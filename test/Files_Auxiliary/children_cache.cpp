/* Copyright 2026 The openPMD Community
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
#include "AuxiliaryTests.hpp"

#include "openPMD/backend/ChildrenCache.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

namespace children_cache
{
using namespace openPMD;

auto children_cache() -> void
{
    SECTION("get_or_create_returns_same_map")
    {
        internal::ChildrenCache cache;
        auto first = cache.get_or_create<int, std::string>();
        first.try_emplace("a", 1);

        auto second = cache.get_or_create<int, std::string>();
        REQUIRE(second.contains("a"));
        REQUIRE(second.at("a") == 1);
        REQUIRE(cache.num_maps() == 1);

        cache.assert_consistent();
    }

    SECTION("different_types_are_independent")
    {
        internal::ChildrenCache cache;
        auto ints = cache.get_or_create<int, std::string>();
        auto doubles = cache.get_or_create<double, std::string>();
        ints.try_emplace("a", 1);
        doubles.try_emplace("a", 2.5);

        REQUIRE(cache.num_maps() == 2);
        REQUIRE(ints.at("a") == 1);
        REQUIRE(doubles.at("a") == 2.5);

        cache.assert_consistent();
    }

    SECTION("erase_removes_from_every_map")
    {
        internal::ChildrenCache cache;
        auto strings = cache.get_or_create<int, std::string>();
        auto numbers = cache.get_or_create<int, std::uint64_t>();
        strings.try_emplace("a", 1);
        strings.try_emplace("b", 2);
        numbers.try_emplace(42, 3);

        cache.erase("a");
        REQUIRE(!strings.contains("a"));
        REQUIRE(strings.contains("b"));

        cache.erase("42");
        REQUIRE(!numbers.contains(42));
        REQUIRE(numbers.empty());

        cache.assert_consistent();
    }

    SECTION("clear_empties_every_map")
    {
        internal::ChildrenCache cache;
        auto strings = cache.get_or_create<int, std::string>();
        auto numbers = cache.get_or_create<int, std::uint64_t>();
        strings.try_emplace("a", 1);
        numbers.try_emplace(42, 3);
        REQUIRE(!cache.empty());

        cache.clear();
        REQUIRE(cache.empty());
        REQUIRE(strings.empty());
        REQUIRE(numbers.empty());

        cache.assert_consistent();
    }

    SECTION("erased_keys_do_not_resurrect")
    {
        internal::ChildrenCache cache;
        auto strings = cache.get_or_create<int, std::string>();
        auto numbers = cache.get_or_create<int, std::uint64_t>();
        strings.try_emplace("a", 1);
        numbers.try_emplace(42, 1);

        cache.erase("a");
        cache.erase("42");

        // Reinserting the same keys must create fresh entries, not revive
        // stale handles from the caches.
        strings.try_emplace("a", 2);
        numbers.try_emplace(42, 2);

        REQUIRE(strings.at("a") == 2);
        REQUIRE(numbers.at(42) == 2);
        cache.assert_consistent();
    }

    SECTION("find_and_contains")
    {
        internal::ChildrenCache cache;
        REQUIRE(!cache.contains<int, std::string>());
        REQUIRE(!cache.find<int, std::string>().has_value());

        auto strings = cache.get_or_create<int, std::string>();
        strings.try_emplace("a", 1);

        REQUIRE(cache.contains<int, std::string>());
        auto found = cache.find<int, std::string>();
        REQUIRE(found.has_value());
        REQUIRE(found->at("a") == 1);
    }
}
} // namespace children_cache
