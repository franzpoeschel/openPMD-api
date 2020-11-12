/* Copyright 2020 Franz Poeschel
 *
 * This file is part of openPMD-api.
 *
 * openPMD-api is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * openPMD-api is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with openPMD-api.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "openPMD/config.hpp"
#if openPMD_HAVE_ADIOS2

#include <adios2.h>
#include <functional>
#include <map>
#include <stddef.h>

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
    public:
        struct AttributeLocation
        {
            adios2::Dims shape;
            size_t offset;
            Datatype dt;
            std::function< void() > destroy;

            AttributeLocation() = delete;
            AttributeLocation( adios2::Dims shape, size_t offset, Datatype dt );

            AttributeLocation( AttributeLocation const & other ) = delete;
            AttributeLocation &
            operator=( AttributeLocation const & other ) = delete;

            AttributeLocation( AttributeLocation && other ) = default;
            AttributeLocation &
            operator=( AttributeLocation && other ) = default;

            ~AttributeLocation();
        };

    private:
        // allocate one large buffer instead of hundreds of single heap
        // allocations
        std::vector< char > m_rawBuffer;
        std::map< std::string, AttributeLocation > m_offsets;

    public:
        explicit PreloadAdiosAttributes() = default;
        PreloadAdiosAttributes( PreloadAdiosAttributes const & other ) = delete;
        PreloadAdiosAttributes &
        operator=( PreloadAdiosAttributes const & other ) = delete;

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
} // namespace detail
} // namespace openPMD

#endif // openPMD_HAVE_ADIOS2