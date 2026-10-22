/* Copyright 2017-2025 Fabian Koller and Franz Poeschel
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

#include "openPMD/backend/ContainerImpl.tpp"

#include "openPMD/CustomHierarchy.hpp"
#include "openPMD/Iteration.hpp"
#include "openPMD/Mesh.hpp"
#include "openPMD/ParticlePatches.hpp"
#include "openPMD/ParticleSpecies.hpp"
#include "openPMD/backend/Container.hpp"
#include "openPMD/backend/HierarchyVisitorImpl.hpp"
#include "openPMD/backend/PatchRecordComponent.hpp"

#include <exception>
#include <optional>
#include <typeinfo>

namespace openPMD
{
template <typename T, typename T_key, typename T_container>
template <typename U>
auto Container<T, T_key, T_container>::materialize_child(
    std::shared_ptr<internal::SharedAttributableData> const &child)
    -> std::optional<U>
{
    if constexpr (std::is_same_v<U, CustomHierarchy>)
    {
        return CustomHierarchy(child);
    }
    else if constexpr (std::is_same_v<U, CustomDataset>)
    {
        if (!child->m_writable.objectType.isDataset())
        {
            return std::nullopt;
        }
        return CustomHierarchy(child).asDataset();
    }
    else
    {
        throw error::WrongAPIUsage(
            "Cannot materialize a child of type '" +
            std::string(typeid(U).name()) +
            "' from the backend hierarchy. This view type has no defined "
            "backend-to-frontend materialization.");
    }
}

template <typename T, typename T_key, typename T_container>
void Container<T, T_key, T_container>::syncContainers(
    internal::object_type::GroupMetaData const &group_data) const
{
    // Need to sync backend objects into the CustomHierarchy instance
    // Need to be a bit sneaky, we must modify the typed map, but this
    // method might be called as const. The cache implements interior
    // mutability, so use that here.

    auto &container_front = container_front_mutable();
    auto &container_back = group_data.m_children;

    auto size_front = container_front.size();
    auto size_back = container_back.size();

    if (size_front > size_back)
    {
        std::stringstream error;
        auto print = [&error](auto const &map) -> std::stringstream & {
            if (map.empty())
            {
                error << "[]";
            }
            else
            {
                error << '[';
                auto it = map.begin();
                error << (it++)->first;
                auto end = map.end();
                for (; it != end; ++it)
                {
                    error << ", " << it->first;
                }
                error << ']';
            }
            return error;
        };
        error << "CustomHierarchy went into illegal state at '"
              << myPath().openPMDPath() << "':\nfront container: ";
        print(container_front) << "\nback container:  ";
        print(container_back) << '\n';
        throw error::Internal(error.str());
    }

    // Not necessary to run GenerationPolicy here again. We could run it for
    // safety purposes, but at a performance impact.

    std::exception_ptr first_materialization_error;

    for (auto const &[key, child] : container_back)
    {
        auto const typed_key =
            internal::children_cache_detail::key_from_string<T_key>(key);

        if (typed_key &&
            container_front.find(*typed_key) != container_front.end())
        {
            continue;
        }

        try
        {
            auto materialized = materialize_child<mapped_type>(child);
            if (materialized)
            {
                assert(typed_key.has_value());
                if (typed_key)
                {
                    auto const inserted = container_front.emplace(
                        *typed_key, std::move(*materialized));
                    assert(inserted.second);
                    (void)inserted;
                }
            }
        }
        catch (...)
        {
            /*
             * Do not clear or rebuild the front map here: already
             * materialized children and their iterators must stay valid. Keep
             * materializing the remaining children and rethrow the first
             * error at the end.
             */
            if (!first_materialization_error)
            {
                first_materialization_error = std::current_exception();
            }
        }
    }

    if (first_materialization_error)
    {
        std::rethrow_exception(first_materialization_error);
    }

    for (auto const &[front_key, child] : container_front)
    {
        (void)child;
        if (container_back.find(key_as_string(front_key)) ==
            container_back.end())
        {
            throw error::Internal(
                "CustomHierarchy went into illegal state at '" +
                myPath().openPMDPath() +
                "': the typed view contains entries that are not present in "
                "the backend hierarchy.");
        }
    }
}

#define OPENPMD_COMMA ,
#define OPENPMD_INSTANTIATE(type) template class Container<type>;

OPENPMD_INSTANTIATE(Mesh)
OPENPMD_INSTANTIATE(MeshRecordComponent)
OPENPMD_INSTANTIATE(ParticlePatches)
OPENPMD_INSTANTIATE(ParticleSpecies)
OPENPMD_INSTANTIATE(PatchRecord)
OPENPMD_INSTANTIATE(PatchRecordComponent)
OPENPMD_INSTANTIATE(Record)
OPENPMD_INSTANTIATE(RecordComponent)
OPENPMD_INSTANTIATE(CustomHierarchy)
OPENPMD_INSTANTIATE(CustomDataset)
OPENPMD_INSTANTIATE(Iteration OPENPMD_COMMA Iteration::IterationIndex_t)
#undef OPENPMD_INSTANTIATE
#undef OPENPMD_COMMA

namespace internal
{
    template class EraseStaleEntries<Mesh>;
    template class EraseStaleEntries<ParticleSpecies>;
    template class EraseStaleEntries<Meshes>;
    template class EraseStaleEntries<Particles>;
} // namespace internal

} // namespace openPMD
