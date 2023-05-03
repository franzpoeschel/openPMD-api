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
#include "openPMD/backend/Attributable.hpp"

namespace openPMD
{
namespace internal
{
    bool MeshesParticlesPath::ignore(const std::string &name) const
    {
        return paths.find(name) != paths.end();
    }
} // namespace internal

CustomHierarchy::CustomHierarchy() = default;
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
    IOHandler()->flush(internal::defaultFlushParams);
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
    flushAttributes(flushParams);
}
} // namespace openPMD
