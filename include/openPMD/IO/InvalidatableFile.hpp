/* Copyright 2018-2021 Franz Poeschel
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

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace openPMD::internal
{

template <typename T>
struct MaybeOwning : std::variant<T, T *>
{
    using variant_t = std::variant<T, T *>;

    explicit MaybeOwning();
    template <typename... Args>
    explicit MaybeOwning(std::in_place_t, Args &&...);
    explicit MaybeOwning(T *);

    operator bool() const;

    auto as_base() -> variant_t &;
    auto as_base() const -> variant_t const &;

    auto operator*() -> T &;
    auto operator*() const -> T const &;

    auto operator->() -> T *;
    auto operator->() const -> T const *;

    auto derive_nonowning() -> MaybeOwning<T>;
};

struct FileState
{
    std::string name;
    std::any backendSpecificState;

    FileState(std::string name);

    FileState(FileState const &other) = delete;
    FileState(FileState &&other) = delete;

    FileState &operator=(FileState const &other) = delete;
    FileState &operator=(FileState &&other) = delete;
};
using SharedFileState = std::shared_ptr<std::optional<FileState>>;
// using SharedFileState = MaybeOwning<std::optional<FileState>>;

template <typename T>
template <typename... Args>
MaybeOwning<T>::MaybeOwning(std::in_place_t, Args &&...args)
    : variant_t(std::in_place_type<T>, std::forward<Args>(args)...)
{}
} // namespace openPMD::internal
