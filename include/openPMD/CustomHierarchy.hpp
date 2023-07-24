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

#include "openPMD/backend/Container.hpp"

#include <iostream>
#include <type_traits>

namespace openPMD
{
class CustomHierarchy;
namespace internal
{
    using CustomHierarchyData = ContainerData<CustomHierarchy>;
}

class CustomHierarchy : public Container<CustomHierarchy>
{
    friend class Iteration;

private:
    using Container_t = Container<CustomHierarchy>;
    using Data_t = typename Container_t::ContainerData;
    static_assert(std::is_same_v<Data_t, internal::CustomHierarchyData>);

protected:
    CustomHierarchy();
    CustomHierarchy(NoInit);

    inline void setData(std::shared_ptr<Data_t> data)
    {
        Container_t::setData(std::move(data));
    }

public:
    CustomHierarchy(CustomHierarchy const &other) = default;
    CustomHierarchy(CustomHierarchy &&other) = default;

    CustomHierarchy &operator=(CustomHierarchy const &) = default;
    CustomHierarchy &operator=(CustomHierarchy &&) = default;
};
} // namespace openPMD
