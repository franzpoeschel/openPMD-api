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
double BaseRecordComponent::unitSI() const
{
    return getAttribute("unitSI").get<double>();
}

BaseRecordComponent &BaseRecordComponent::resetDatatype(Datatype d)
{
    if (written())
        throw std::runtime_error(
            "A Records Datatype can not (yet) be changed after it has been "
            "written.");

    auto &rc = get();
    if (rc.m_dataset.has_value())
    {
        rc.m_dataset.value().dtype = d;
    }
    else
    {
        rc.m_dataset = Dataset{d, {1}};
    }
    return *this;
}

Datatype BaseRecordComponent::getDatatype() const
{
    auto &rc = get();
    if (rc.m_dataset.has_value())
    {
        return rc.m_dataset.value().dtype;
    }
    else
    {
        return Datatype::UNDEFINED;
    }
}

bool BaseRecordComponent::constant() const
{
    return get().m_isConstant;
}

ChunkTable BaseRecordComponent::availableChunks()
{
    auto &rc = get();
    if (rc.m_isConstant)
    {
        if (!rc.m_dataset.has_value())
        {
            return ChunkTable{};
        }
        Offset offset(rc.m_dataset.value().extent.size(), 0);
        return ChunkTable{{std::move(offset), rc.m_dataset.value().extent}};
    }
    containingIteration().open();
    Parameter<Operation::AVAILABLE_CHUNKS> param;
    IOTask task(this, param);
    IOHandler()->enqueue(task);
    IOHandler()->flush(internal::defaultFlushParams);
    return std::move(*param.chunks);
}

BaseRecordComponent::BaseRecordComponent(
    std::shared_ptr<internal::BaseRecordComponentData> data)
    : Attributable{data}, m_baseRecordComponentData{std::move(data)}
{}

BaseRecordComponent::BaseRecordComponent() : Attributable{nullptr}
{
    Attributable::setData(m_baseRecordComponentData);
}
} // namespace openPMD
