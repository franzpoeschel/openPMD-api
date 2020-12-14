/* Copyright 2017-2020 Fabian Koller
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
#include "openPMD/backend/Attributable.hpp"
#include "openPMD/auxiliary/StringManip.hpp"

#include <iostream>
#include <set>
#include <complex>


namespace openPMD
{
namespace internal
{
AttributableData::AttributableData()
        : m_writable{ std::make_shared< Writable >(this) },
          abstractFilePosition{m_writable->abstractFilePosition.get()},
          IOHandler{m_writable->IOHandler.get()},
          parent{m_writable->parent},
          m_attributes{std::make_shared< A_MAP >()}
{ }

AttributableData::AttributableData(AttributableData const& rhs)
        : m_writable{rhs.m_writable},
          abstractFilePosition{rhs.m_writable->abstractFilePosition.get()},
          IOHandler{rhs.m_writable->IOHandler.get()},
          parent{rhs.m_writable->parent},
          m_attributes{rhs.m_attributes}
{ }

AttributableData&
AttributableData::operator=(AttributableData const& a)
{
    if( this != &a )
    {
        m_writable = a.m_writable;
        abstractFilePosition = a.m_writable->abstractFilePosition.get();
        IOHandler = a.m_writable->IOHandler.get();
        parent = a.m_writable->parent;
        m_attributes = a.m_attributes;
    }
    return *this;
}
}

#if 0
AttributableInternal::AttributableInternal(AttributableInternal const& rhs)
        : internal::AttributableData{ rhs }
{ 
}

AttributableInternal&
AttributableInternal::operator=(AttributableInternal const& a)
{
    internal::AttributableData::operator=( a );
    return *this;
}
#endif
} // openPMD
