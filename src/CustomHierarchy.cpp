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

#include "openPMD/Dataset.hpp"
#include "openPMD/Error.hpp"
#include "openPMD/IO/AbstractIOHandler.hpp"
#include "openPMD/IO/Access.hpp"
#include "openPMD/IO/IOTask.hpp"
#include "openPMD/Mesh.hpp"
#include "openPMD/ParticleSpecies.hpp"
#include "openPMD/RecordComponent.hpp"
#include "openPMD/Series.hpp"
#include "openPMD/auxiliary/StringManip.hpp"
#include "openPMD/backend/Attributable.hpp"
#include "openPMD/backend/BaseRecord.hpp"
#include "openPMD/backend/MeshRecordComponent.hpp"
#include "openPMD/backend/Writable.hpp"

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>

namespace
{
template <typename Iterator>
std::string
concatWithSep(Iterator &&begin, Iterator const &end, std::string const &sep)
{
    if (begin == end)
    {
        return "";
    }
    std::stringstream res;
    res << *(begin++);
    for (; begin != end; ++begin)
    {
        res << sep << *begin;
    }
    return res.str();
}

std::string
concatWithSep(std::vector<std::string> const &v, std::string const &sep)
{
    return concatWithSep(v.begin(), v.end(), sep);
}

// Not specifying std::regex_constants::optimize here, only using it where
// it makes sense to.
constexpr std::regex_constants::syntax_option_type regex_flags =
    std::regex_constants::egrep;

template <typename OutParam>
void setDefaultMeshesParticlesPath(
    std::vector<std::string> const &meshes,
    std::vector<std::string> const &particles,
    OutParam &writeTarget)
{
    std::regex is_default_path_specification("[[:alnum:]_]+/", regex_flags);
    constexpr char const *default_default_mesh = "meshes";
    constexpr char const *default_default_particle = "particles";
    for (auto [vec, defaultPath, default_default] :
         {std::make_tuple(
              &meshes, &writeTarget.m_defaultMeshesPath, default_default_mesh),
          std::make_tuple(
              &particles,
              &writeTarget.m_defaultParticlesPath,
              default_default_particle)})
    {
        bool set_default = true;
        /*
         * The first eligible path in meshesPath/particlesPath is used as
         * the default, "meshes"/"particles" otherwise.
         */
        for (auto const &path : *vec)
        {
            if (std::regex_match(path, is_default_path_specification))
            {
                *defaultPath = openPMD::auxiliary::replace_last(path, "/", "");
                set_default = false;
                break;
            }
        }
        if (set_default)
        {
            *defaultPath = default_default;
        }
    }
}

bool anyPathRegexMatches(
    std::regex regex,
    std::vector<std::string> const &path,
    std::string const &name)
{
    /*
     * /group/meshes/E is a mesh if the meshes path contains:
     *
     * 1) '/group/meshes/' (absolute path to mesh container)
     * 2) '/group/meshes/E' (absolute path to mesh itself)
     * 3) 'meshes/' (relative path to mesh container)
     *
     * The potential fourth option 'E' (relative path to mesh itself)
     * is not supported. ("Anything that is named 'E' is a mesh" is not
     * really a semantic that we want to explicitly support.)
     * '/' is never a valid meshes path.
     *
     * All this analogously for particles path.
     */
    std::vector<std::string> pathsToMatch = {
        /* option 2) from above */
        "/" + (path.empty() ? "" : concatWithSep(path, "/") + "/") + name};
    if (!path.empty())
    {
        // option 1) from above
        pathsToMatch.emplace_back("/" + concatWithSep(path, "/") + "/");

        // option 3 from above
        pathsToMatch.emplace_back(*path.rbegin() + "/");
    }
    return std::any_of(
        pathsToMatch.begin(),
        pathsToMatch.end(),
        [&regex](std::string const &candidate_path) {
            return std::regex_match(candidate_path, regex);
        });
}
} // namespace

namespace openPMD
{
namespace internal
{
    MeshesParticlesPath::MeshesParticlesPath(
        std::vector<std::string> const &meshes,
        std::vector<std::string> const &particles)
    {
        std::regex is_default_path_specification("[[:alnum:]_]+/", regex_flags);
        for (auto [target_regex, vec] :
             {std::make_tuple(&this->meshRegex, &meshes),
              std::make_tuple(&this->particleRegex, &particles)})
        {
            if (vec->empty())
            {
                *target_regex = std::regex(
                    /* does not match anything */ "a^",
                    regex_flags | std::regex_constants::optimize);
                continue;
            }
            auto begin = vec->begin();
            std::stringstream build_regex;
            build_regex << '(' << *begin++ << ')';
            for (; begin != vec->end(); ++begin)
            {
                build_regex << "|(" << *begin << ')';
            }
            auto regex_string = build_regex.str();
            // std::cout << "Using regex string: " << regex_string << std::endl;
            *target_regex = std::regex(
                std::move(regex_string),
                regex_flags | std::regex_constants::optimize);
        }
        setDefaultMeshesParticlesPath(meshes, particles, *this);
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
        return anyPathRegexMatches(particleRegex, path, name);
    }
    bool MeshesParticlesPath::isMesh(
        std::vector<std::string> const &path, std::string const &name) const
    {
        return anyPathRegexMatches(meshRegex, path, name);
    }

    CustomHierarchyData::CustomHierarchyData()
    {
        syncAttributables();
    }

    void CustomHierarchyData::syncAttributables()
    {
        /*
         * m_embeddeddatasets and its friends should point to the same instance
         * of Attributable.
         */
        for (auto p : std::initializer_list<Attributable *>{
                 &m_embeddedDatasets, &m_embeddedMeshes, &m_embeddedParticles})
        {
            p->m_attri->asSharedPtrOfAttributable() =
                this->asSharedPtrOfAttributable();
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

void CustomHierarchy::readNonscalarMesh(
    EraseStaleMeshes &map, std::string const &mesh_name)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_ATTS> aList;

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

void CustomHierarchy::readScalarMesh(
    EraseStaleMeshes &map, std::string const &mesh_name)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_PATHS> pList;

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

void CustomHierarchy::readParticleSpecies(
    EraseStaleParticles &map, std::string const &species_name)
{
    Parameter<Operation::OPEN_PATH> pOpen;
    Parameter<Operation::LIST_PATHS> pList;

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

    std::deque<std::string> constantComponentsPushback;
    auto &data = get();
    EraseStaleMeshes meshesMap(data.m_embeddedMeshes);
    EraseStaleParticles particlesMap(data.m_embeddedParticles);
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
            break;
        }
        case internal::ContainedType::Mesh: {
            try
            {
                readNonscalarMesh(meshesMap, path);
            }
            catch (error::ReadError const &err)
            {
                std::cerr << "Cannot read mesh at location '"
                          << myPath().printGroup() << "/" << path
                          << "' and will skip them due to read error:\n"
                          << err.what() << std::endl;
                meshesMap.forget(path);
            }
            break;
        }
        case internal::ContainedType::Particle: {
            try
            {
                readParticleSpecies(particlesMap, path);
            }
            catch (error::ReadError const &err)
            {
                std::cerr << "Cannot read particle species at location '"
                          << myPath().printGroup() << "/" << path
                          << "' and will skip them due to read error:\n"
                          << err.what() << std::endl;
                particlesMap.forget(path);
            }
            break;
        }
        }
    }
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
            readScalarMesh(meshesMap, path);
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
    auto &data = get();
    if (access::write(IOHandler()->m_frontendAccess))
    {
        if (!meshes.empty())
        {
            (*this)[mpp.m_defaultMeshesPath];
        }

        if (!particles.empty())
        {
            (*this)[mpp.m_defaultParticlesPath];
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
    for (auto &[name, mesh] : data.m_embeddedMeshes)
    {
        if (!mpp.isMesh(currentPath, name))
        {
            std::string extend_meshes_path;
            // Check if this can be covered by shorthand notation
            // (e.g. meshesPath == "meshes/")
            if (!currentPath.empty() &&
                *currentPath.rbegin() == mpp.m_defaultMeshesPath)
            {
                extend_meshes_path = *currentPath.rbegin() + "/";
            }
            else
            {
                // Otherwise use full path
                extend_meshes_path = "/" +
                    (currentPath.empty()
                         ? ""
                         : concatWithSep(currentPath, "/") + "/") +
                    name;
            }
            mpp.collectNewMeshesPaths.emplace(std::move(extend_meshes_path));
        }
        mesh.flush(name, flushParams);
    }
    for (auto &[name, particleSpecies] : data.m_embeddedParticles)
    {
        if (!mpp.isParticle(currentPath, name))
        {
            std::string extend_particles_path;
            if (!currentPath.empty() &&
                *currentPath.rbegin() == mpp.m_defaultParticlesPath)
            {
                // Check if this can be covered by shorthand notation
                // (e.g. particlesPath == "particles/")
                extend_particles_path = *currentPath.rbegin() + "/";
            }
            else
            {
                // Otherwise use full path
                extend_particles_path = "/" +
                    (currentPath.empty()
                         ? ""
                         : concatWithSep(currentPath, "/") + "/") +
                    name;
            }
            mpp.collectNewParticlesPaths.emplace(
                std::move(extend_particles_path));
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
    std::vector<std::string> meshesPaths = s.meshesPaths(),
                             particlesPaths = s.particlesPaths();
    internal::MeshesParticlesPath mpp(meshesPaths, particlesPaths);
    std::vector<std::string> currentPath;
    flush_internal(flushParams, mpp, currentPath);
    if (!mpp.collectNewMeshesPaths.empty() ||
        !mpp.collectNewParticlesPaths.empty())
    {
        for (auto [newly_added_paths, vec] :
             {std::make_pair(&mpp.collectNewMeshesPaths, &meshesPaths),
              std::make_pair(&mpp.collectNewParticlesPaths, &particlesPaths)})
        {
            std::transform(
                newly_added_paths->begin(),
                newly_added_paths->end(),
                std::back_inserter(*vec),
                [](auto const &pair) { return pair; });
        }
        s.setMeshesPath(meshesPaths);
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
    auto check = [](auto const &container) {
        for (auto const &pair : container)
        {
            if (pair.second.dirtyRecursive())
            {
                return true;
            }
        }
        return false;
    };
    auto &data = get();
    return check(data.m_embeddedMeshes) || check(data.m_embeddedParticles) ||

        /*
         * Need to check this, too. It might be that the `meshes` alias has not
         * been synced yet with the "meshes" subgroup.
         * The CustomHierarchy object needs to be flushed in order for that to
         * happen (or the "meshes" group needs to be accessed explicitly via
         * operator[]()).
         */
        check(meshes) || check(particles) || check(data.m_embeddedDatasets) ||
        check(*this);
}

auto CustomHierarchy::operator[](key_type &&key) -> mapped_type &
{
    return bracketOperatorImpl(std::move(key));
}

auto CustomHierarchy::operator[](key_type const &key) -> mapped_type &
{
    return bracketOperatorImpl(key);
}

template <typename ContainedType>
auto CustomHierarchy::asContainerOf() -> Container<ContainedType> &
{
    if constexpr (std::is_same_v<ContainedType, CustomHierarchy>)
    {
        return *static_cast<Container<CustomHierarchy> *>(this);
    }
    else if constexpr (std::is_same_v<ContainedType, Mesh>)
    {
        return get().m_embeddedMeshes;
    }
    else if constexpr (std::is_same_v<ContainedType, ParticleSpecies>)
    {
        return get().m_embeddedParticles;
    }
    else if constexpr (std::is_same_v<ContainedType, RecordComponent>)
    {
        return get().m_embeddedDatasets;
    }
    else
    {
        static_assert(
            auxiliary::dependent_false_v<ContainedType>,
            "[CustomHierarchy::asContainerOf] Type parameter must be "
            "one of: CustomHierarchy, RecordComponent, Mesh, "
            "ParticleSpecies.");
    }
}

template auto CustomHierarchy::asContainerOf<CustomHierarchy>()
    -> Container<CustomHierarchy> &;
template auto CustomHierarchy::asContainerOf<RecordComponent>()
    -> Container<RecordComponent> &;
template auto CustomHierarchy::asContainerOf<Mesh>() -> Container<Mesh> &;
template auto CustomHierarchy::asContainerOf<ParticleSpecies>()
    -> Container<ParticleSpecies> &;

/*
 * This method implements the usual job of ::operator[](), but additionally
 * ensures that returned entries are properly linked with ::particles and
 * ::meshes.
 */
template <typename KeyType>
auto CustomHierarchy::bracketOperatorImpl(KeyType &&provided_key)
    -> mapped_type &
{
    auto &cont = container();
    auto find_special_key =
        [&cont, &provided_key, this](
            std::string const &special_key,
            auto &alias,
            auto &&embeddedAccessor) -> std::optional<mapped_type *> {
        if (provided_key != special_key)
        {
            return std::nullopt;
        }
        if (auto it = cont.find(provided_key); it != cont.end())
        {
            if (it->second.m_attri->get() != alias.m_attri->get() ||
                embeddedAccessor(it->second)->m_containerData.get() !=
                    alias.m_containerData.get())
            {
                /*
                 * This might happen if a user first creates a custom group
                 * "fields" and sets the default meshes path as "fields"
                 * only later.
                 * If the CustomHierarchy::meshes alias carries no data yet,
                 * we can just redirect it to that group now.
                 * Otherwise, we need to fail.
                 */
                if (alias.empty() && alias.attributes().empty())
                {
                    alias.m_containerData =
                        embeddedAccessor(it->second)->m_containerData;
                    alias.m_attri->asSharedPtrOfAttributable() =
                        it->second.m_attri->asSharedPtrOfAttributable();
                    return &it->second;
                }
                throw error::WrongAPIUsage(
                    "Found a group '" + provided_key + "' at path '" +
                    myPath().printGroup() +
                    "' which is not synchronous with mesh/particles alias "
                    "despite '" +
                    special_key +
                    "' being the default meshes/particles path. This can "
                    "have happened because setting default "
                    "meshes/particles path too late (after first flush). "
                    "If that's not the case, this is likely an internal "
                    "bug.");
            }
            return &it->second;
        }
        else
        {
            auto *res =
                &Container::operator[](std::forward<KeyType>(provided_key));
            embeddedAccessor(*res)->m_containerData = alias.m_containerData;
            res->m_attri->asSharedPtrOfAttributable() =
                alias.m_attri->asSharedPtrOfAttributable();
            res->m_customHierarchyData->syncAttributables();
            return res;
        }
    };

    /*
     * @todo Buffer this somehow while still ensuring that changed meshesPath
     * or particlesPath will be recorded.
     */
    struct
    {
        std::string m_defaultMeshesPath;
        std::string m_defaultParticlesPath;
    } defaultPaths;

    {
        auto const &series = retrieveSeries();
        auto meshes_paths = series.meshesPaths();
        auto particles_paths = series.particlesPaths();
        setDefaultMeshesParticlesPath(
            meshes_paths, particles_paths, defaultPaths);
    }

    if (auto res = find_special_key(
            defaultPaths.m_defaultMeshesPath,
            meshes,
            [](auto &group) {
                return &group.m_customHierarchyData->m_embeddedMeshes;
            });
        res.has_value())
    {
        return **res;
    }
    if (auto res = find_special_key(
            defaultPaths.m_defaultParticlesPath,
            particles,
            [](auto &group) {
                return &group.m_customHierarchyData->m_embeddedParticles;
            });
        res.has_value())
    {
        return **res;
    }
    else
    {
        return (*this).Container::operator[](
            std::forward<KeyType>(provided_key));
    }
}
} // namespace openPMD
