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
        loadString( T *, adios2::Engine &, adios2::Variable< T > & )
        {
        }

        template<>
        void
        loadString< std::string >(
            std::string * ptr,
            adios2::Engine & engine,
            adios2::Variable< std::string > & var )
        {
            engine.Get( var, *ptr, adios2::Mode::Sync );
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
                    if( shape.size() > 0 )
                    {
                        throw std::runtime_error(
                            "[ADIOS2] ADIOS does not support global string "
                            "arrays." );
                    }
                    loadString< T >( dest, engine, var );
                }
                else
                {
                    engine.Get( var, dest, adios2::Mode::Deferred );
                }
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
            addAttribute( dt, std::move( variable.first ) );
        }

        // PHASE 2: get offsets for attributes in buffer
        std::map< Datatype, size_t > offsets;
        size_t currentOffset = 0;
        size_t stringOffset = 0;
        GetAlignment __alignment;
        GetSize __size;
        VariableShape __shape;
        for( auto & pair : attributesByType )
        {
            if( pair.first == Datatype::STRING )
            {
                for( std::string & name : pair.second )
                {
                    adios2::Dims shape =
                        __shape.operator()< std::string >( IO, name );
                    if( shape.size() > 0 )
                    {
                        throw std::runtime_error(
                            "[ADIOS2] ADIOS does not support global string "
                            "arrays." );
                    }
                    AttributeLocation location;
                    location.offset = stringOffset;
                    location.dt = Datatype::STRING;
                    location.shape = {};
                    m_offsets[ std::move( name ) ] = std::move( location );
                    ++stringOffset;
                }
                continue;
            }
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
                AttributeLocation location;
                location.offset = currentOffset;
                location.dt = pair.first;
                location.shape = std::move( shape );
                m_offsets[ std::move( name ) ] = std::move( location );
                currentOffset += elements * size;
            }
        }
        // now, currentOffset is the number of bytes that we need to allocate
        // PHASE 3: allocate new buffer and schedule loads
        m_rawBuffer.resize( currentOffset );
        m_stringBuffer.resize( stringOffset );
        ScheduleLoad __schedule;
        for( auto const & pair : m_offsets )
        {
            if( pair.second.dt == Datatype::STRING )
            {
                __schedule.operator()< std::string >(
                    IO,
                    engine,
                    pair.first,
                    reinterpret_cast< char * >(
                        &m_stringBuffer[ pair.second.offset ] ) );
            }
            else
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
    }

    template<>
    AttributeWithShape< std::string >
    PreloadAdiosAttributes::getAttribute< std::string >(
        std::string const & name ) const
    {
        auto it = m_offsets.find( name );
        if( it == m_offsets.end() )
        {
            throw std::runtime_error(
                "[ADIOS2] Requested attribute not found: " + name );
        }
        AttributeLocation const & location = it->second;
        if( location.dt != Datatype::STRING )
        {
            throw std::runtime_error(
                "[ADIOS2] Wrong datatype for attribute: " + name );
        }
        AttributeWithShape< std::string > res;
        res.shape = location.shape;
        res.data = &m_stringBuffer[ location.offset ];
        return res;
    }
} // namespace detail
} // namespace openPMD