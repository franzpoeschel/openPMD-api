#pragma once

#include <adios2.h>
#include <functional>
#include <map>

#include "openPMD/Datatype.hpp"

namespace openPMD
{
namespace detail
{
    class PreloadAdiosAttributes
    {
        struct AttributeLocation
        {
            size_t offset;
            Datatype dt;
        };
        // allocate one large buffer instead of hundreds of single heap
        // allocations
        // std::unique_ptr< char, std::function< void( char * ) > > m_rawBuffer;
        std::vector< char > m_rawBuffer;
        std::map< std::string, AttributeLocation > m_offsets;

    public:
        explicit PreloadAdiosAttributes() = default;
        void
        preloadAttributes( adios2::IO & IO, adios2::Engine & engine );
        template< typename T >
        T *
        getAttribute( std::string const & name );
    };
} // namespace detail
} // namespace openPMD