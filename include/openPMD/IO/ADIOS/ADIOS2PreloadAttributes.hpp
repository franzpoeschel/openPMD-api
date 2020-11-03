#pragma once

#include <adios2.h>
#include <functional>
#include <map>

#include "openPMD/Datatype.hpp"

namespace openPMD
{
namespace detail
{
    template< typename T >
    struct AttributeWithShape
    {
        adios2::Dims shape;
        T const * data;
    };

    class PreloadAdiosAttributes
    {
        struct AttributeLocation
        {
            adios2::Dims shape;
            size_t offset;
            Datatype dt;
        };
        // allocate one large buffer instead of hundreds of single heap
        // allocations
        // std::unique_ptr< char, std::function< void( char * ) > > m_rawBuffer;
        std::vector< char > m_rawBuffer;
        std::vector< std::string > m_stringBuffer;
        std::map< std::string, AttributeLocation > m_offsets;

    public:
        explicit PreloadAdiosAttributes() = default;
        void
        preloadAttributes( adios2::IO & IO, adios2::Engine & engine );
        template< typename T >
        AttributeWithShape< T >
        getAttribute( std::string const & name ) const;
    };

    template< typename T >
    AttributeWithShape< T >
    PreloadAdiosAttributes::getAttribute( std::string const & name ) const
    {
        auto it = m_offsets.find( name );
        if( it == m_offsets.end() )
        {
            throw std::runtime_error(
                "[ADIOS2] Requested attribute not found: " + name );
        }
        AttributeLocation const & location = it->second;
        if( location.dt != determineDatatype< T >() )
        {
            throw std::runtime_error(
                "[ADIOS2] Wrong datatype for attribute: " + name );
        }
        AttributeWithShape< T > res;
        res.shape = location.shape;
        res.data =
            reinterpret_cast< T const * >( &m_rawBuffer[ location.offset ] );
        return res;
    }

    template<>
    AttributeWithShape< std::string >
    PreloadAdiosAttributes::getAttribute< std::string >(
        std::string const & name ) const;
} // namespace detail
} // namespace openPMD