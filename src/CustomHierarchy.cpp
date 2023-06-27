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
#include "openPMD/ParticleSpecies.hpp"
#include "openPMD/RecordComponent.hpp"
#include "openPMD/Series.hpp"
#include "openPMD/auxiliary/StringManip.hpp"
#include "openPMD/backend/Attributable.hpp"

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <regex>
#include <sstream>

namespace openPMD
{

namespace
{
    std::string
    concatWithSep(std::vector<std::string> const &v, std::string const &sep)
    {
        switch (v.size())
        {
        case 0:
            return "";
        case 1:
            return *v.begin();
        default:
            break;
        }
        std::stringstream res;
        auto it = v.begin();
        res << *it++;
        for (; it != v.end(); ++it)
        {
            res << sep << *it;
        }
        return res.str();
    }

    // Not specifying std::regex_constants::optimize here, only using it where
    // it makes sense to.
    constexpr std::regex_constants::syntax_option_type regex_flags =
        std::regex_constants::egrep;
} // namespace

namespace internal
{
    namespace
    {
        bool anyPathRegexMatches(
            std::map<std::string, std::regex> regexes,
            std::vector<std::string> const &path,
            std::string const &name)
        {
            std::string parentPath =
                (path.empty() ? "" : concatWithSep(path, "/")) + "/";
            std::string fullPath = path.empty() ? name : parentPath + name;
            return std::any_of(
                regexes.begin(),
                regexes.end(),
                [&parentPath, &fullPath](auto const &regex) {
                    return std::regex_match(parentPath, regex.second) ||
                        std::regex_match(fullPath, regex.second);
                });
        }
    } // namespace

    MeshesParticlesPath::MeshesParticlesPath(
        std::vector<std::string> const &meshes,
        std::vector<std::string> const &particles)
    {
        for (auto [deque, vec] :
             {std::make_pair(&this->meshesPath, &meshes),
              std::make_pair(&this->particlesPath, &particles)})
        {
            std::transform(
                vec->begin(),
                vec->end(),
                std::inserter(*deque, deque->begin()),
                [](auto const &str) {
                    return std::make_pair(
                        str,
                        std::regex(
                            str, regex_flags | std::regex_constants::optimize));
                });
        }
    }

    ContainedType MeshesParticlesPath::determineType(
        std::vector<std::string> const &path, std::string const &name) const
    {
        if (isMesh(path, name))
        {
            return ContainedType::Mesh;
        }
        else if (isParticle(path, name))
        {
            return ContainedType::Particle;
        }
        else
        {
            return ContainedType::Group;
        }
    }

    bool MeshesParticlesPath::isParticle(
        std::vector<std::string> const &path, std::string const &name) const
    {
        return anyPathRegexMatches(particlesPath, path, name);
    }
    bool MeshesParticlesPath::isMesh(
        std::vector<std::string> const &path, std::string const &name) const
    {
        return anyPathRegexMatches(meshesPath, path, name);
    }

    CustomHierarchyData::CustomHierarchyData()
    {
        /*
         * m_embeddeddatasets and its friends should point to the same instance
         * of Attributable Can only use a non-owning pointer in here in order to
         * avoid shared pointer cycles. When handing this object out to users,
         * we create a copy that has a proper owning pointer (see
         * CustomHierarchy::datasets()).
         */
        for (auto p : std::initializer_list<Attributable *>{
                 &m_embeddedDatasets, &m_embeddedMeshes, &m_embeddedParticles})
        {
            (*p).Attributable::setData(
                std::shared_ptr<AttributableData>(this, [](auto const *) {}));
        }
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

void CustomHierarchy::readNonscalarMesh(std::string const &mesh_name)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_ATTS> aList;

    internal::EraseStaleEntries<decltype(get().m_embeddedMeshes)> map{
        get().m_embeddedMeshes};
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

void CustomHierarchy::readScalarMesh(std::string const &mesh_name)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_PATHS> pList;

    auto &data = get();

    internal::EraseStaleEntries<decltype(data.m_embeddedMeshes)> map{
        data.m_embeddedMeshes};

    Parameter<Operation::OPEN_DATASET> dOpen;
    Mesh &m = map[mesh_name];
    dOpen.name = mesh_name;
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

void CustomHierarchy::readParticleSpecies(std::string const &species_name)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_PATHS> pList;

    internal::EraseStaleEntries<decltype(get().m_embeddedParticles)> map{
        get().m_embeddedParticles};
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
        std::cerr << "Cannot read particle species with name '" << species_name
                  << "' and will skip it due to read error:\n"
                  << err.what() << std::endl;
        map.forget(species_name);
    }
}

// @todo make this flexible again
constexpr char const *defaultMeshesPath = "meshes";
constexpr char const *defaultParticlesPath = "particles";

void CustomHierarchy::read(internal::MeshesParticlesPath const &mpp)
{
    std::vector<std::string> currentPath;
    read(mpp, currentPath);
}

void CustomHierarchy::read(
    internal::MeshesParticlesPath const &mpp,
    std::vector<std::string> &currentPath)
{
    /*
     * Convention for CustomHierarchy::flush and CustomHierarchy::read:
     * Path is created/opened already at entry point of method, method needs
     * to create/open path for contained subpaths.
     */

    Parameter<Operation::LIST_PATHS> pList;
    IOHandler()->enqueue(IOTask(this, pList));

    Attributable::readAttributes(ReadMode::FullyReread);
    Parameter<Operation::LIST_DATASETS> dList;
    IOHandler()->enqueue(IOTask(this, dList));
    IOHandler()->flush(internal::defaultFlushParams);

    meshes.dirty() = false;
    particles.dirty() = false;
    std::deque<std::string> constantComponentsPushback;
    for (auto const &path : *pList.paths)
    {
        switch (mpp.determineType(currentPath, path))
        {
        case internal::ContainedType::Group: {
            Parameter<Operation::OPEN_PATH> pOpen;
            pOpen.path = path;
            auto &subpath = this->operator[](path);
            IOHandler()->enqueue(IOTask(&subpath, pOpen));
            currentPath.emplace_back(path);
            subpath.read(mpp, currentPath);
            currentPath.pop_back();
            if (subpath.size() == 0 && subpath.containsAttribute("shape") &&
                subpath.containsAttribute("value"))
            {
                // This is not a group, but a constant record component
                // Writable::~Writable() will deal with removing this from the
                // backend again.
                constantComponentsPushback.push_back(path);
                container().erase(path);
            }
            else
            {
                if (path == defaultMeshesPath)
                {
                    synchronizeContainers(
                        subpath.get().m_embeddedMeshes, meshes);
                }
                // no else, they might be the same
                if (path == defaultParticlesPath)
                {
                    synchronizeContainers(
                        subpath.get().m_embeddedParticles, particles);
                }
            }
            break;
        }
        case internal::ContainedType::Mesh: {
            try
            {
                readNonscalarMesh(path);
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
            break;
        }
        case internal::ContainedType::Particle: {
            try
            {
                readParticleSpecies(path);
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
            break;
        }
        }
    }
    auto &data = get();
    for (auto const &path : *dList.datasets)
    {
        switch (mpp.determineType(currentPath, path))
        {
            // Group is a bit of an internal misnomer here, it just means that
            // it matches neither meshes nor particles path
        case internal::ContainedType::Group: {
            auto &rc = data.m_embeddedDatasets[path];
            Parameter<Operation::OPEN_DATASET> dOpen;
            dOpen.name = path;
            IOHandler()->enqueue(IOTask(&rc, dOpen));
            IOHandler()->flush(internal::defaultFlushParams);
            rc.written() = false;
            rc.resetDataset(Dataset(*dOpen.dtype, *dOpen.extent));
            rc.written() = true;
            rc.read();
            break;
        }
        case internal::ContainedType::Mesh:
            readScalarMesh(path);
            break;
        case internal::ContainedType::Particle:
            std::cerr
                << "[Warning] Dataset found at '"
                << (concatWithSep(currentPath, "/") + "/" + path)
                << " that matches one of the given particle paths. A particle "
                   "species is always a group, never a dataset. Will skip."
                << std::endl;
            break;
        }
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

template <typename T>
void CustomHierarchy::synchronizeContainers(
    Container<T> &target, Container<T> &source)
{
    if (target.m_containerData.get() == source.m_containerData.get())
    {
        return;
    }
    auto &target_container = target.container();
    for (auto &pair : source.container())
    {
        pair.second.linkHierarchy(target.writable());
        target_container.emplace(std::move(pair));
    }
    source.container().clear();
    auto &target_attributes = target.m_attri->m_attributes;
    for (auto &pair : source.m_attri->m_attributes)
    {
        target_attributes.emplace(std::move(pair));
    }
    source.m_attri->m_attributes.clear();
    source.setData(target.m_containerData);
    // We need to do this since we redirect the Attributable pointers for some
    // members:
    source.Attributable::setData(target.m_attri);
}

void CustomHierarchy::flush_internal(
    internal::FlushParams const &flushParams,
    internal::MeshesParticlesPath &mpp,
    std::vector<std::string> currentPath)
{
    /*
     * Convention for CustomHierarchy::flush and CustomHierarchy::read:
     * Path is created/opened already at entry point of method, method needs
     * to create/open path for contained subpaths.
     */

    // No need to do anything in access::readOnly since meshes and particles
    // are initialized as aliases for subgroups at parsing time
    if (access::write(IOHandler()->m_frontendAccess))
    {
        if (!meshes.empty())
        {
            auto defaultMeshes =
                (*this)[defaultMeshesPath].asContainerOf<Mesh>();
            synchronizeContainers(defaultMeshes, meshes);
        }

        if (!particles.empty())
        {
            auto defaultParticles =
                (*this)[defaultParticlesPath].asContainerOf<ParticleSpecies>();
            synchronizeContainers(defaultParticles, particles);
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
        currentPath.emplace_back(name);
        subpath.flush_internal(flushParams, mpp, currentPath);
        currentPath.pop_back();
    }
    auto &data = get();
    for (auto &[name, mesh] : data.m_embeddedMeshes)
    {
        if (!mpp.isMesh(currentPath, name))
        {
            std::string fullPath = currentPath.empty()
                ? name
                : concatWithSep(currentPath, "/") + "/" + name;
            mpp.meshesPath.emplace(fullPath, std::regex(fullPath, regex_flags));
        }
        mesh.flush(name, flushParams);
    }
    for (auto &[name, particleSpecies] : data.m_embeddedParticles)
    {
        if (!mpp.isParticle(currentPath, name))
        {
            std::string fullPath = currentPath.empty()
                ? name
                : concatWithSep(currentPath, "/") + "/" + name;
            mpp.particlesPath.emplace(
                fullPath, std::regex(fullPath, regex_flags));
        }
        particleSpecies.flush(name, flushParams);
    }
    for (auto &[name, dataset] : get().m_embeddedDatasets)
    {
        dataset.flush(name, flushParams);
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

    Series s = this->retrieveSeries();
    internal::MeshesParticlesPath mpp(s.meshesPaths(), s.particlesPaths());
    auto num_meshes_paths = mpp.meshesPath.size();
    auto num_particles_paths = mpp.particlesPath.size();
    std::vector<std::string> currentPath;
    flush_internal(flushParams, mpp, currentPath);
    std::vector<std::string> meshesPaths, particlesPaths;
    for (auto [map, vec] :
         {std::make_pair(&mpp.meshesPath, &meshesPaths),
          std::make_pair(&mpp.particlesPath, &particlesPaths)})
    {
        std::transform(
            map->begin(),
            map->end(),
            std::back_inserter(*vec),
            [](auto const &pair) { return pair.first; });
    }
    if (meshesPaths.size() > num_meshes_paths)
    {
        s.setMeshesPath(meshesPaths);
    }
    if (particlesPaths.size() > num_particles_paths)
    {
        s.setParticlesPath(particlesPaths);
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

template <typename ContainedType>
auto CustomHierarchy::asContainerOf() -> Container<ContainedType>
{
    if constexpr (std::is_same_v<ContainedType, CustomHierarchy>)
    {
        return *static_cast<Container<CustomHierarchy> *>(this);
    }
    else
    {
        Container<ContainedType> res = [&data = get()]() {
            if constexpr (std::is_same_v<ContainedType, Mesh>)
            {
                return data.m_embeddedMeshes;
            }
            else if constexpr (std::is_same_v<ContainedType, ParticleSpecies>)
            {
                return data.m_embeddedParticles;
            }
            else if constexpr (std::is_same_v<ContainedType, RecordComponent>)
            {
                return data.m_embeddedDatasets;
            }
            else
            {
                static_assert(
                    auxiliary::dependent_false_v<ContainedType>,
                    "[CustomHierarchy::asContainerOf] Type parameter must be "
                    "one of: CustomHierarchy, RecordComponent, Mesh, "
                    "ParticleSpecies.");
            }
        }();
        res.Attributable::setData(m_customHierarchyData);
        return res;
    }
}

template auto CustomHierarchy::asContainerOf<CustomHierarchy>()
    -> Container<CustomHierarchy>;
template auto CustomHierarchy::asContainerOf<RecordComponent>()
    -> Container<RecordComponent>;
template auto CustomHierarchy::asContainerOf<Mesh>() -> Container<Mesh>;
template auto CustomHierarchy::asContainerOf<ParticleSpecies>()
    -> Container<ParticleSpecies>;
} // namespace openPMD
