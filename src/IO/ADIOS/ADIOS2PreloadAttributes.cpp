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
        template< typename T >
        void
        initString( T *, size_t )
        {
        }

        template<>
        void
        initString< std::string >( std::string * ptr, size_t items )
        {
            for( size_t i = 0; i < items; ++i )
            {
                ptr[ i ] = std::string();
            }
        }

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
                char * buffer )
            {
                adios2::Variable< T > var = IO.InquireVariable< T >( name );
                if( !var )
                {
                    throw std::runtime_error(
                        "[ADIOS2] Variable not found: " + name );
                }
                adios2::Dims shape = var.Shape();
                adios2::Dims offset( shape.size(), 0 );
                if( shape.size() > 0 )
                {
                    var.SetSelection( { offset, shape } );
                }
                T * dest = reinterpret_cast< T * >( buffer );
                if
#if __cplusplus > 201402L
                    constexpr
#endif
                    ( std::is_same< T, std::string >::value )
                {
                    size_t items = 1;
                    for( auto extent : shape )
                    {
                        items *= extent;
                    }
                    initString< T >( dest, items );
                }
                engine.Get(
                    var,
                    reinterpret_cast< T * >( buffer ),
                    adios2::Mode::Deferred );
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
                return AttributeTypes< T >::alignment();
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

        size_t
        getMaxAlignment()
        {
            size_t res = 0;
            GetAlignment ga;
            for( auto dt : openPMD_Datatypes )
            {
                res = std::max( res, switchType< size_t >( dt, ga ) );
            }
            return res;
        }

        size_t maxAlignment = getMaxAlignment();
    } // namespace

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
            if(dt == Datatype::STRING)
            {
                continue;
            }
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
                AttributeLocation location;
                location.offset = currentOffset;
                location.dt = pair.first;
                m_offsets[ std::move( name ) ] = location;
                size_t elements = 1;
                for( auto extent : shape )
                {
                    elements *= extent;
                }
                currentOffset += elements * size;
            }
        }
        // now, currentOffset is the number of bytes that we need to allocate
        // PHASE 3: allocate new buffer and schedule loads
        m_rawBuffer.resize( currentOffset );
        ScheduleLoad __schedule;
        for( auto const & pair : m_offsets )
        {
            switchType(
                pair.second.dt,
                __schedule,
                IO,
                engine,
                pair.first,
                &m_rawBuffer[ pair.second.offset ] );
        }
    }
} // namespace detail
} // namespace openPMD