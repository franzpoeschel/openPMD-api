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
#pragma once

#include "openPMD/IO/AbstractIOHandler.hpp"
#include "openPMD/Mesh.hpp"
#include "openPMD/ParticleSpecies.hpp"
#include "openPMD/RecordComponent.hpp"
#include "openPMD/backend/Container.hpp"
#include "openPMD/backend/Writable.hpp"

#include <iostream>
#include <set>
#include <string>
#include <type_traits>

namespace openPMD
{
class CustomHierarchy;
namespace internal
{
    struct MeshesParticlesPath
    {
        std::set<std::string> paths;
        [[nodiscard]] bool ignore(std::string const &name) const;
    };

    struct CustomHierarchyData : ContainerData<CustomHierarchy>
    {
        explicit CustomHierarchyData();

        Container<RecordComponent> m_embeddedDatasets;
    };
} // namespace internal

class CustomHierarchy : public Container<CustomHierarchy>
{
    friend class Iteration;
    friend class Container<CustomHierarchy>;

private:
    using Container_t = Container<CustomHierarchy>;
    using Data_t = internal::CustomHierarchyData;
    static_assert(std::is_base_of_v<Container_t::ContainerData, Data_t>);

    std::shared_ptr<Data_t> m_customHierarchyData;

    void init();

    [[nodiscard]] Data_t &get()
    {
        return *m_customHierarchyData;
    }
    [[nodiscard]] Data_t const &get() const
    {
        return *m_customHierarchyData;
    }

protected:
    CustomHierarchy();
    CustomHierarchy(NoInit);

    inline void setData(std::shared_ptr<Data_t> data)
    {
        m_customHierarchyData = data;
        Container_t::setData(std::move(data));
    }

    void read(internal::MeshesParticlesPath const &);

    void flush(std::string const &path, internal::FlushParams const &) override;

    void linkHierarchy(Writable &w) override;

public:
    CustomHierarchy(CustomHierarchy const &other) = default;
    CustomHierarchy(CustomHierarchy &&other) = default;

    CustomHierarchy &operator=(CustomHierarchy const &) = default;
    CustomHierarchy &operator=(CustomHierarchy &&) = default;

    Container<RecordComponent> datasets();

    Container<Mesh> meshes{};
    Container<ParticleSpecies> particles{}; // particleSpecies?
};
} // namespace openPMD
