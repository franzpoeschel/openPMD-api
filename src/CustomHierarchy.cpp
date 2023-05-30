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
#include "openPMD/Series.hpp"
#include "openPMD/auxiliary/StringManip.hpp"
#include "openPMD/backend/Attributable.hpp"

#include <algorithm>
#include <deque>
#include <memory>

namespace openPMD
{
namespace internal
{
    bool MeshesParticlesPath::ignore(const std::string &name) const
    {
        auto no_slashes = [](std::string const &str) {
            return auxiliary::trim(str, [](char const &c) { return c == '/'; });
        };
        return (meshesPath.has_value() &&
                name == no_slashes(meshesPath.value())) ||
            (particlesPath.has_value() &&
             name == no_slashes(particlesPath.value()));
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
    meshes.writable().ownKeyWithinParent = "meshes";
    particles.writable().ownKeyWithinParent = "particles";
}
CustomHierarchy::CustomHierarchy(NoInit) : Container_t(NoInit())
{}

void CustomHierarchy::readMeshes(std::string const &meshesPath)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_PATHS> pList;

    pOpen.path = meshesPath;
    IOHandler()->enqueue(IOTask(&meshes, pOpen));

    meshes.readAttributes(ReadMode::FullyReread);

    internal::EraseStaleEntries<decltype(meshes)> map{meshes};

    /* obtain all non-scalar meshes */
    IOHandler()->enqueue(IOTask(&meshes, pList));
    IOHandler()->flush(internal::defaultFlushParams);

    Parameter<Operation::LIST_ATTS> aList;
    for (auto const &mesh_name : *pList.paths)
    {
        Mesh &m = map[mesh_name];
        pOpen.path = mesh_name;
        aList.attributes->clear();
        IOHandler()->enqueue(IOTask(&m, pOpen));
        IOHandler()->enqueue(IOTask(&m, aList));
        IOHandler()->flush(internal::defaultFlushParams);

        auto att_begin = aList.attributes->begin();
        auto att_end = aList.attributes->end();
        auto value = std::find(att_begin, att_end, "value");
        auto shape = std::find(att_begin, att_end, "shape");
        if (value != att_end && shape != att_end)
        {
            MeshRecordComponent &mrc = m;
            IOHandler()->enqueue(IOTask(&mrc, pOpen));
            IOHandler()->flush(internal::defaultFlushParams);
            mrc.get().m_isConstant = true;
        }
        m.read();
        try
        {
            m.read();
        }
        catch (error::ReadError const &err)
        {
            std::cerr << "Cannot read mesh with name '" << mesh_name
                      << "' and will skip it due to read error:\n"
                      << err.what() << std::endl;
            map.forget(mesh_name);
        }
    }

    /* obtain all scalar meshes */
    Parameter<Operation::LIST_DATASETS> dList;
    IOHandler()->enqueue(IOTask(&meshes, dList));
    IOHandler()->flush(internal::defaultFlushParams);

    Parameter<Operation::OPEN_DATASET> dOpen;
    for (auto const &mesh_name : *dList.datasets)
    {
        Mesh &m = map[mesh_name];
        dOpen.name = mesh_name;
        IOHandler()->enqueue(IOTask(&m, dOpen));
        IOHandler()->flush(internal::defaultFlushParams);
        MeshRecordComponent &mrc = m;
        IOHandler()->enqueue(IOTask(&mrc, dOpen));
        IOHandler()->flush(internal::defaultFlushParams);
        mrc.written() = false;
        mrc.resetDataset(Dataset(*dOpen.dtype, *dOpen.extent));
        mrc.written() = true;
        try
        {
            m.read();
        }
        catch (error::ReadError const &err)
        {
            std::cerr << "Cannot read mesh with name '" << mesh_name
                      << "' and will skip it due to read error:\n"
                      << err.what() << std::endl;
            map.forget(mesh_name);
        }
    }
}

void CustomHierarchy::readParticles(std::string const &particlesPath)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_PATHS> pList;

    pOpen.path = particlesPath;
    IOHandler()->enqueue(IOTask(&particles, pOpen));

    particles.readAttributes(ReadMode::FullyReread);

    /* obtain all particle species */
    pList.paths->clear();
    IOHandler()->enqueue(IOTask(&particles, pList));
    IOHandler()->flush(internal::defaultFlushParams);

    internal::EraseStaleEntries<decltype(particles)> map{particles};
    for (auto const &species_name : *pList.paths)
    {
        ParticleSpecies &p = map[species_name];
        pOpen.path = species_name;
        IOHandler()->enqueue(IOTask(&p, pOpen));
        IOHandler()->flush(internal::defaultFlushParams);
        try
        {
            p.read();
        }
        catch (error::ReadError const &err)
        {
            std::cerr << "Cannot read particle species with name '"
                      << species_name
                      << "' and will skip it due to read error:\n"
                      << err.what() << std::endl;
            map.forget(species_name);
        }
    }
}

void CustomHierarchy::read(internal::MeshesParticlesPath const &mpp)
{
    /*
     * Convention for CustomHierarchy::flush and CustomHierarchy::read:
     * Path is created/opened already at entry point of method, method needs
     * to create/open path for contained subpaths.
     */

    Parameter<Operation::LIST_PATHS> pList;
    IOHandler()->enqueue(IOTask(this, pList));
    IOHandler()->flush(internal::defaultFlushParams);

    auto thisGroupHasMeshesOrParticles =
        [&pList](std::optional<std::string> meshesOrParticlesPath) -> bool {
        if (!meshesOrParticlesPath.has_value())
        {
            return false;
        }
        auto no_slashes = [](std::string const &str) {
            return auxiliary::trim(str, [](char const &c) { return c == '/'; });
        };
        std::string look_for = no_slashes(meshesOrParticlesPath.value());
        auto const &paths = *pList.paths;
        return std::find_if(
                   paths.begin(),
                   paths.end(),
                   [&no_slashes, &look_for](std::string const &entry) {
                       return no_slashes(entry) == look_for;
                   }) != paths.end();
    };

    if (thisGroupHasMeshesOrParticles(mpp.meshesPath))
    {
        try
        {
            readMeshes(mpp.meshesPath.value());
        }
        catch (error::ReadError const &err)
        {
            std::cerr << "Cannot read meshes at location '"
                      << myPath().printGroup()
                      << "' and will skip them due to read error:\n"
                      << err.what() << std::endl;
            meshes = {};
            meshes.dirty() = false;
        }
    }
    else
    {
        meshes.dirty() = false;
    }

    if (thisGroupHasMeshesOrParticles(mpp.particlesPath))
    {
        try
        {
            readParticles(mpp.particlesPath.value());
        }
        catch (error::ReadError const &err)
        {
            std::cerr << "Cannot read particles at location '"
                      << myPath().printGroup()
                      << "' and will skip them due to read error:\n"
                      << err.what() << std::endl;
            particles = {};
            particles.dirty() = false;
        }
    }
    else
    {
        particles.dirty() = false;
    }

    Attributable::readAttributes(ReadMode::FullyReread);
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

/*
 * @todo Avoid repeated calls of retrieveSeries, the hierarchy might be deep.
 */
void CustomHierarchy::flush(
    std::string const & /* path */, internal::FlushParams const &flushParams)
{
    /*
     * Convention for CustomHierarchy::flush and CustomHierarchy::read:
     * Path is created/opened already at entry point of method, method needs
     * to create/open path for contained subpaths.
     */

    if (access::readOnly(IOHandler()->m_frontendAccess))
    {
        for (auto &m : meshes)
            m.second.flush(m.first, flushParams);
        for (auto &species : particles)
            species.second.flush(species.first, flushParams);
    }
    else
    {
        /* Find the root point [Series] of this file,
         * meshesPath and particlesPath are stored there */
        Series s = retrieveSeries();

        if (!meshes.empty())
        {
            if (!s.containsAttribute("meshesPath"))
            {
                s.setMeshesPath("meshes/");
                s.flushMeshesPath();
            }
            meshes.flush(s.meshesPath(), flushParams);
            for (auto &m : meshes)
                m.second.flush(m.first, flushParams);
        }
        else
        {
            meshes.dirty() = false;
        }

        if (!particles.empty())
        {
            if (!s.containsAttribute("particlesPath"))
            {
                s.setParticlesPath("particles/");
                s.flushParticlesPath();
            }
            particles.flush(s.particlesPath(), flushParams);
            for (auto &species : particles)
                species.second.flush(species.first, flushParams);
        }
        else
        {
            particles.dirty() = false;
        }

        flushAttributes(flushParams);
    }

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
}

void CustomHierarchy::linkHierarchy(Writable &w)
{
    Attributable::linkHierarchy(w);
    meshes.linkHierarchy(this->writable());
    particles.linkHierarchy(this->writable());
}

bool CustomHierarchy::dirtyRecursive() const
{
    if (dirty())
    {
        return true;
    }
    if (particles.dirty() || meshes.dirty())
    {
        return true;
    }
    for (auto const &pair : particles)
    {
        if (pair.second.dirtyRecursive())
        {
            return true;
        }
    }
    for (auto const &pair : meshes)
    {
        if (pair.second.dirtyRecursive())
        {
            return true;
        }
    }
    for (auto const &pair : *this)
    {
        if (pair.second.dirtyRecursive())
        {
            return true;
        }
    }
    for (auto const &pair : get().m_embeddedDatasets)
    {
        if (pair.second.dirtyRecursive())
        {
            return true;
        }
    }
    return false;
}

Container<RecordComponent> CustomHierarchy::datasets()
{
    Container<RecordComponent> res = get().m_embeddedDatasets;
    res.Attributable::setData(m_customHierarchyData);
    return res;
}
} // namespace openPMD
