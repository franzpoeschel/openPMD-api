/* Copyright 2017-2021 Franz Poeschel.
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

#include "openPMD/IO/InvalidatableFile.hpp"
#include "openPMD/auxiliary/Variant.hpp"

namespace openPMD::internal
{

template <typename T>
MaybeOwning<T>::MaybeOwning() : MaybeOwning(static_cast<T *>(nullptr))
{}

template <typename T>
MaybeOwning<T>::MaybeOwning(T *val) : variant_t(std::move(val))
{}

template <typename T>
MaybeOwning<T>::operator bool() const
{
    return std::visit(
        auxiliary::overloaded{
            [](T const &) { return true; },
            [](T const *val) { return static_cast<bool>(val); }},
        as_base());
}

template <typename T>
auto MaybeOwning<T>::as_base() -> variant_t &
{
    return *this;
}

template <typename T>
auto MaybeOwning<T>::as_base() const -> variant_t const &
{
    return *this;
}

template <typename T>
auto MaybeOwning<T>::operator*() const -> T const &
{
    return std::visit(
        auxiliary::overloaded{
            [](T const &val) -> T const & { return val; },
            [](T const *val) -> T const & { return *val; }},
        as_base());
}

template <typename T>
auto MaybeOwning<T>::operator*() -> T &
{
    return const_cast<T &>(
        static_cast<MaybeOwning<T> const *>(this)->operator*());
}

template <typename T>
auto MaybeOwning<T>::operator->() -> T *
{
    return &operator*();
}

template <typename T>
auto MaybeOwning<T>::operator->() const -> T const *
{
    return &operator*();
}

template <typename T>
auto MaybeOwning<T>::derive_nonowning() -> MaybeOwning<T>
{
    return MaybeOwning<T> { &operator*() };
}

template struct MaybeOwning<std::optional<FileState>>;

FileState::FileState(std::string name_in) : name(std::move(name_in))
{}
} // namespace openPMD::internal
