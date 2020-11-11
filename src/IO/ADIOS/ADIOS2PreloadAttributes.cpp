#include "openPMD/IO/ADIOS/ADIOS2PreloadAttributes.hpp"

#include <cstdlib>
#include <iostream>
#include <numeric>

#include "openPMD/Datatype.hpp"
#include "openPMD/IO/ADIOS/ADIOS2Auxiliary.hpp"
#include "openPMD/auxiliary/StringManip.hpp"
#include <type_traits>

namespace openPMD
{
namespace detail
{
    namespace
    {
        template< typename T, typename = void >
        struct AttributeTypes;

        template< typename T >
        struct AttributeTypes<
            T,
            typename std::enable_if< IsTrivialType< T >::val >::type >
        {
            static size_t
            alignment()
            {
                return alignof( T );
            }

            static size_t
            size()
            {
                return sizeof( T );
            }

            static adios2::Dims
            shape( adios2::IO & IO, std::string const & name )
            {
                auto var = IO.InquireVariable< T >( name );
                if( !var )
                {
                    throw std::runtime_error(
                        "[ADIOS2] Variable not found: " + name );
                }
                return var.Shape();
            }

            static void
            scheduleLoad(
                adios2::IO & IO,
                adios2::Engine & engine,
                std::string const & name,
                char * buffer,
                PreloadAdiosAttributes::AttributeLocation & location )
            {
                adios2::Variable< T > var = IO.InquireVariable< T >( name );
                if( !var )
                {
                    throw std::runtime_error(
                        "[ADIOS2] Variable not found: " + name );
                }
                adios2::Dims const & shape = location.shape;
                adios2::Dims offset( shape.size(), 0 );
                if( shape.size() > 0 )
                {
                    var.SetSelection( { offset, shape } );
                }
                T * dest = reinterpret_cast< T * >( buffer );
                size_t numItems = 1;
                for( auto extent : shape )
                {
                    numItems *= extent;
                }
                new( dest ) T[ numItems ];
                location.destroy = [ dest, numItems ]() {
                    for( size_t i = 0; i < numItems; ++i )
                    {
                        dest[ i ].~T();
                    }
                };
                engine.Get( var, dest, adios2::Mode::Deferred );
            }
        };

        template< typename T >
        struct AttributeTypes<
            T,
            typename std::enable_if< !IsTrivialType< T >::val >::type >
        {
            static size_t
            alignment()
            {
                return 0; // we're not interested in those
            }

            static size_t
            size()
            {
                return 0; // we're not interested in those
            }

            template< typename... Args >
            static adios2::Dims
            shape( Args &&... )
            {
                throw std::runtime_error( "[ADIOS2] Unreachable!" );
            }

            template< typename... Args >
            static void
            scheduleLoad( Args &&... )
            {
                throw std::runtime_error( "[ADIOS2] Unreachable!" );
            }
        };

        struct GetAlignment
        {
            template< typename T >
            constexpr size_t
            operator()()
            {
                return AttributeTypes< T >::alignment();
            }

            template< unsigned long, typename... Args >
            constexpr size_t
            operator()( Args &&... )
            {
                return 0;
            }
        };

        struct GetSize
        {
            template< typename T >
            constexpr size_t
            operator()()
            {
                return AttributeTypes< T >::size();
            }

            template< unsigned long, typename... Args >
            constexpr size_t
            operator()( Args &&... )
            {
                return 0;
            }
        };

        struct ScheduleLoad
        {
            template< typename T, typename... Args >
            void
            operator()( Args &&... args )
            {
                AttributeTypes< T >::scheduleLoad(
                    std::forward< Args >( args )... );
            }

            template< unsigned long, typename... Args >
            void
            operator()( Args &&... )
            {
                throw std::runtime_error( "[ADIOS2] Unknown datatype." );
            }
        };

        struct VariableShape
        {
            template< typename T, typename... Args >
            adios2::Dims
            operator()( Args &&... args )
            {
                return AttributeTypes< T >::shape(
                    std::forward< Args >( args )... );
            }

            template< unsigned long n, typename... Args >
            adios2::Dims
            operator()( Args &&... )
            {
                return {};
            }
        };
    } // namespace

    PreloadAdiosAttributes::AttributeLocation::AttributeLocation(
        adios2::Dims shape_in,
        size_t offset_in,
        Datatype dt_in )
        : shape( std::move( shape_in ) )
        , offset( offset_in )
        , dt( dt_in )
    {
    }

    PreloadAdiosAttributes::AttributeLocation::~AttributeLocation()
    {
        /*
         * If the object has been moved from, this may be empty.
         * Or else, if no custom destructor has been emplaced.
         */
        if( destroy )
        {
            destroy();
        }
    }

    void
    PreloadAdiosAttributes::preloadAttributes(
        adios2::IO & IO,
        adios2::Engine & engine )
    {
        m_offsets.clear();
        std::map< Datatype, std::vector< std::string > > attributesByType;
        auto addAttribute =
            [ &attributesByType ]( Datatype dt, std::string name ) {
                constexpr size_t reserve = 10;
                auto it = attributesByType.find( dt );
                if( it == attributesByType.end() )
                {
                    it = attributesByType.emplace_hint(
                        it, dt, std::vector< std::string >() );
                    it->second.reserve( reserve );
                }
                it->second.push_back( std::move( name ) );
            };
        // PHASE 1: collect names of available attributes by ADIOS datatype
        for( auto & variable : IO.AvailableVariables() )
        {
            if( auxiliary::ends_with( variable.first, "/__data__" ) )
            {
                continue;
            }
            // this will give us basic types only, no fancy vectors or similar
            Datatype dt = fromADIOS2Type( IO.VariableType( variable.first ) );
            addAttribute( dt, std::move( variable.first ) );
        }

        // PHASE 2: get offsets for attributes in buffer
        std::map< Datatype, size_t > offsets;
        size_t currentOffset = 0;
        GetAlignment __alignment;
        GetSize __size;
        VariableShape __shape;
        for( auto & pair : attributesByType )
        {
            size_t alignment = switchType< size_t >( pair.first, __alignment );
            size_t size = switchType< size_t >( pair.first, __size );
            // go to next offset with valid alignment
            size_t modulus = currentOffset % alignment;
            if( modulus > 0 )
            {
                currentOffset += alignment - modulus;
            }
            for( std::string & name : pair.second )
            {
                adios2::Dims shape =
                    switchType< adios2::Dims >( pair.first, __shape, IO, name );
                size_t elements = 1;
                for( auto extent : shape )
                {
                    elements *= extent;
                }
                m_offsets.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple( std::move( name ) ),
                    std::forward_as_tuple(
                        std::move( shape ), currentOffset, pair.first ) );
                currentOffset += elements * size;
            }
        }
        // now, currentOffset is the number of bytes that we need to allocate
        // PHASE 3: allocate new buffer and schedule loads
        m_rawBuffer.resize( currentOffset );
        ScheduleLoad __schedule;
        for( auto & pair : m_offsets )
        {
            switchType(
                pair.second.dt,
                __schedule,
                IO,
                engine,
                pair.first,
                &m_rawBuffer[ pair.second.offset ],
                pair.second );
        }
    }
} // namespace detail
} // namespace openPMD