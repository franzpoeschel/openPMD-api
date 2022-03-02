/* Copyright 2017-2021 Fabian Koller
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
#include "openPMD/backend/BaseRecordComponent.hpp"
#include "openPMD/Iteration.hpp"

namespace openPMD
{
template <typename Attributable_t>
double BaseRecordComponent<Attributable_t>::unitSI() const
{
    return this->getAttribute("unitSI").template get<double>();
}

template <typename Attributable_t>
BaseRecordComponent<Attributable_t> &
BaseRecordComponent<Attributable_t>::resetDatatype(Datatype d)
{
    if (this->written())
        throw std::runtime_error(
            "A Records Datatype can not (yet) be changed after it has been "
            "written.");

    get().m_dataset.dtype = d;
    return *this;
}

template <typename Attributable_t>
Datatype BaseRecordComponent<Attributable_t>::getDatatype() const
{
    return get().m_dataset.dtype;
}

template <typename Attributable_t>
bool BaseRecordComponent<Attributable_t>::constant() const
{
    return get().m_isConstant;
}

template <typename Attributable_t>
ChunkTable BaseRecordComponent<Attributable_t>::availableChunks()
{
    auto &rc = get();
    if (rc.m_isConstant)
    {
        Offset offset(rc.m_dataset.extent.size(), 0);
        return ChunkTable{{std::move(offset), rc.m_dataset.extent}};
    }
    this->containingIteration().open();
    Parameter<Operation::AVAILABLE_CHUNKS> param;
    IOTask task(this, param);
    this->IOHandler()->enqueue(task);
    this->IOHandler()->flush();
    return std::move(*param.chunks);
}

template <typename Attributable_t>
BaseRecordComponent<Attributable_t>::BaseRecordComponent(
    std::shared_ptr<DataClass> data)
    : Attributable{data}, m_baseRecordComponentData{std::move(data)}
{}

template <typename Attributable_t>
BaseRecordComponent<Attributable_t>::BaseRecordComponent()
    : Attributable{nullptr}
{
    Attributable::setData(m_baseRecordComponentData);
}

template class BaseRecordComponent<Attributable>;
} // namespace openPMD
