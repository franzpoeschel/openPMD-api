/* Copyright 2021 Franz Poeschel
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

#include "openPMD/snapshots/ContainerTraits.hpp"
#include "openPMD/snapshots/IteratorTraits.hpp"

#include <utility>

/*
 * Private header not included in user code.
 * Implements the Iterator interface for the random-access workflow.
 */

namespace openPMD
{
namespace detail
{
    template <typename iterator_t>
    using iterator_to_value_type =
        // do NOT remove const from type
        std::remove_reference_t<decltype(*std::declval<iterator_t>())>;
}

template <typename iterator_t_in>
class RandomAccessIterator
    : public AbstractSeriesIterator<
          RandomAccessIterator<iterator_t_in>,
          detail::iterator_to_value_type<iterator_t_in>>
{
public:
    using iterator_t = iterator_t_in;

private:
    friend class RandomAccessIteratorContainer;
    template <typename>
    friend class OpaqueSeriesIterator;
    template <
        typename ConcreteIteratorClass,
        typename ValueType,
        typename... ConstructorArgs>
    friend auto from_concrete_iterator(ConstructorArgs &&...args)
        -> OpaqueSeriesIterator<ValueType>;

    using parent_t = AbstractSeriesIterator<
        RandomAccessIterator<iterator_t>,
        detail::iterator_to_value_type<iterator_t>>;

    RandomAccessIterator(iterator_t it);
    RandomAccessIterator(iterator_t it, iterator_t begin, iterator_t end);

    /* Internal iterator */
    iterator_t m_it;
    struct InfoForAutomaticallyOpeningIterations
    {
        iterator_t m_begin;
        iterator_t m_end;
    };
    std::optional<InfoForAutomaticallyOpeningIterations>
        m_automaticallyOpenIterations;

    static constexpr auto is_const() -> bool
    {
        return std::is_const_v<std::remove_reference_t<decltype(*m_it)>>;
    }

    template <bool boundary_is_inclusive>
    auto increment_operator_impl(
        iterator_t &(iterator_t::*incr)(),
        iterator_t InfoForAutomaticallyOpeningIterations::*boundary)
        -> RandomAccessIterator &;

public:
    using typename parent_t::value_type;

    ~RandomAccessIterator() override;

    RandomAccessIterator(RandomAccessIterator const &other) = default;
    RandomAccessIterator(RandomAccessIterator &&other) noexcept(
        noexcept(iterator_t(std::declval<iterator_t &&>()))) = default;

    RandomAccessIterator &
    operator=(RandomAccessIterator const &other) = default;
    RandomAccessIterator &operator=(RandomAccessIterator &&other) noexcept(
        noexcept(std::declval<iterator_t>().operator=(
            std::declval<iterator_t &&>()))) = default;

    auto operator*() -> value_type &;
    auto operator*() const -> value_type const &;

    auto operator++() -> RandomAccessIterator &;
    auto operator--() -> RandomAccessIterator &;
    auto operator++(int) -> RandomAccessIterator;
    auto operator--(int) -> RandomAccessIterator;

    using parent_t::operator!=;
    bool operator==(RandomAccessIterator const &other) const;
};
} // namespace openPMD
