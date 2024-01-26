/* Copyright 2021 Franz Poeschel
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

#include "openPMD/Iteration.hpp"
#include "openPMD/SeriesIterator.hpp"
namespace openPMD
{

// @todo const iteration?
class RandomAccessSnapshots
    : public AbstractSeriesIterator<RandomAccessSnapshots>
{
private:
    friend class RandomAccessSnapshotsContainer;
    using iterator_t = Container<Iteration, difference_type>::iterator;
    iterator_t m_it;

    inline RandomAccessSnapshots(iterator_t it) : m_it(it)
    {}

    using parent_t = AbstractSeriesIterator<RandomAccessSnapshots>;

public:
    using parent_t::operator*;
    inline value_type const &operator*() const
    {
        return *m_it;
    }

    inline RandomAccessSnapshots &operator++()
    {
        ++m_it;
        return *this;
    }

    inline RandomAccessSnapshots &operator--()
    {
        --m_it;
        return *this;
    }

    inline bool operator==(RandomAccessSnapshots const &other) const
    {
        return m_it == other.m_it;
    }
};
} // namespace openPMD
