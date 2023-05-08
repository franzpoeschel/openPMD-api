/* Copyright 2023 Franz Poeschel
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

#include "openPMD/CustomHierarchy.hpp"
#include "openPMD/IO/AbstractIOHandler.hpp"
#include "openPMD/IO/Access.hpp"
#include "openPMD/IO/IOTask.hpp"
#include "openPMD/RecordComponent.hpp"
#include "openPMD/backend/Attributable.hpp"

#include <deque>
#include <memory>

namespace openPMD
{
namespace internal
{
    bool MeshesParticlesPath::ignore(const std::string &name) const
    {
        return paths.find(name) != paths.end();
    }

    CustomHierarchyData::CustomHierarchyData()
    {
        /*
         * m_embeddeddatasets should point to the same instance of Attributable
         * Can only use a non-owning pointer in here in order to avoid shared
         * pointer cycles.
         * When handing this object out to users, we create a copy that has a
         * proper owning pointer (see CustomHierarchy::datasets()).
         */
        m_embeddedDatasets.Attributable::setData(
            std::shared_ptr<AttributableData>(this, [](auto const *) {}));
    }
} // namespace internal

CustomHierarchy::CustomHierarchy()
{
    setData(std::make_shared<Data_t>());
}
CustomHierarchy::CustomHierarchy(NoInit) : Container_t(NoInit())
{}

void CustomHierarchy::read(internal::MeshesParticlesPath const &mpp)
{
    /*
     * Convention for CustomHierarchy::flush and CustomHierarchy::read:
     * Path is created/opened already at entry point of method, method needs
     * to create/open path for contained subpaths.
     */
    Attributable::readAttributes(ReadMode::FullyReread);
    Parameter<Operation::LIST_PATHS> pList;
    IOHandler()->enqueue(IOTask(this, pList));
    Parameter<Operation::LIST_DATASETS> dList;
    IOHandler()->enqueue(IOTask(this, dList));
    IOHandler()->flush(internal::defaultFlushParams);
    std::deque<std::string> constantComponentsPushback;
    for (auto const &path : *pList.paths)
    {
        if (mpp.ignore(path))
        {
            continue;
        }
        Parameter<Operation::OPEN_PATH> pOpen;
        pOpen.path = path;
        auto subpath = this->operator[](path);
        IOHandler()->enqueue(IOTask(&subpath, pOpen));
        subpath.read(mpp);
        if (subpath.size() == 0 && subpath.containsAttribute("shape") &&
            subpath.containsAttribute("value"))
        {
            // This is not a group, but a constant record component
            // Writable::~Writable() will deal with removing this from the
            // backend again.
            constantComponentsPushback.push_back(path);
            container().erase(path);
        }
    }
    auto &data = get();
    for (auto const &path : *dList.datasets)
    {
        auto &rc = data.m_embeddedDatasets[path];
        Parameter<Operation::OPEN_DATASET> dOpen;
        dOpen.name = path;
        IOHandler()->enqueue(IOTask(&rc, dOpen));
        IOHandler()->flush(internal::defaultFlushParams);
        rc.written() = false;
        rc.resetDataset(Dataset(*dOpen.dtype, *dOpen.extent));
        rc.written() = true;
        rc.read();
    }

    for (auto const &path : constantComponentsPushback)
    {
        auto &rc = data.m_embeddedDatasets[path];
        Parameter<Operation::OPEN_PATH> pOpen;
        pOpen.path = path;
        IOHandler()->enqueue(IOTask(&rc, pOpen));
        rc.get().m_isConstant = true;
        rc.read();
    }
}

void CustomHierarchy::flush(
    std::string const & /* path */, internal::FlushParams const &flushParams)
{
    /*
     * Convention for CustomHierarchy::flush and CustomHierarchy::read:
     * Path is created/opened already at entry point of method, method needs
     * to create/open path for contained subpaths.
     */
    Parameter<Operation::CREATE_PATH> pCreate;
    for (auto &[name, subpath] : *this)
    {
        if (!subpath.written())
        {
            pCreate.path = name;
            IOHandler()->enqueue(IOTask(&subpath, pCreate));
        }
        subpath.flush(name, flushParams);
    }
    for (auto &[name, dataset] : get().m_embeddedDatasets)
    {
        dataset.flush(name, flushParams);
    }
    flushAttributes(flushParams);
}

Container<RecordComponent> CustomHierarchy::datasets()
{
    Container<RecordComponent> res = get().m_embeddedDatasets;
    res.Attributable::setData(m_customHierarchyData);
    return res;
}
} // namespace openPMD
