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
#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace openPMD
{
template <typename T, typename T_key, typename T_container>
class Container;
} // namespace openPMD

namespace openPMD::internal
{
class SharedAttributableData;

namespace children_cache_detail
{
    template <typename>
    constexpr bool dependent_false = false;

    template <typename Key>
    auto key_from_string(std::string const &key) -> std::optional<Key>
    {
        if constexpr (std::is_same_v<Key, std::string>)
        {
            return key;
        }
        else if constexpr (std::is_integral_v<Key>)
        {
            if (key.empty())
            {
                return std::nullopt;
            }
            std::size_t pos = 0;
            try
            {
                if constexpr (std::is_signed_v<Key>)
                {
                    auto const value = std::stoll(key, &pos);
                    if (pos != key.size())
                    {
                        return std::nullopt;
                    }
                    if (value < static_cast<long long>(
                                    std::numeric_limits<Key>::min()) ||
                        value > static_cast<long long>(
                                    std::numeric_limits<Key>::max()))
                    {
                        return std::nullopt;
                    }
                    return static_cast<Key>(value);
                }
                else
                {
                    auto const value = std::stoull(key, &pos);
                    if (pos != key.size())
                    {
                        return std::nullopt;
                    }
                    if (value > static_cast<unsigned long long>(
                                    std::numeric_limits<Key>::max()))
                    {
                        return std::nullopt;
                    }
                    return static_cast<Key>(value);
                }
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
        else
        {
            static_assert(
                dependent_false<Key>,
                "ChildrenCache does not know how to convert this key type "
                "from its canonical string representation.");
        }
    }

    template <typename Key>
    auto key_to_string(Key const &key) -> std::string
    {
        if constexpr (std::is_same_v<Key, std::string>)
        {
            return key;
        }
        else if constexpr (std::is_integral_v<Key>)
        {
            return std::to_string(key);
        }
        else
        {
            static_assert(
                dependent_false<Key>,
                "ChildrenCache does not know how to convert this key type "
                "to its canonical string representation.");
        }
    }
} // namespace children_cache_detail

/** Non-owning, restricted view of one typed map inside a ChildrenCache.
 *
 * The view deliberately does not expose erase(), clear(), or operator[].
 * Structural removal is only possible through ChildrenCache::erase() and
 * ChildrenCache::clear(), which update every materialized map.
 */
template <typename T, typename Key>
class ChildrenCacheMap
{
    friend class ChildrenCache;

public:
    using map_type = std::map<Key, T>;
    using key_type = typename map_type::key_type;
    using mapped_type = typename map_type::mapped_type;
    using value_type = typename map_type::value_type;
    using size_type = typename map_type::size_type;
    using iterator = typename map_type::iterator;
    using const_iterator = typename map_type::const_iterator;
    using reverse_iterator = typename map_type::reverse_iterator;
    using const_reverse_iterator = typename map_type::const_reverse_iterator;

    auto begin() noexcept -> iterator
    {
        return m_map->begin();
    }
    auto begin() const noexcept -> const_iterator
    {
        return m_map->begin();
    }
    auto end() noexcept -> iterator
    {
        return m_map->end();
    }
    auto end() const noexcept -> const_iterator
    {
        return m_map->end();
    }
    auto rbegin() noexcept -> reverse_iterator
    {
        return m_map->rbegin();
    }
    auto rbegin() const noexcept -> const_reverse_iterator
    {
        return m_map->rbegin();
    }
    auto rend() noexcept -> reverse_iterator
    {
        return m_map->rend();
    }
    auto rend() const noexcept -> const_reverse_iterator
    {
        return m_map->rend();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return m_map->empty();
    }
    [[nodiscard]] auto size() const noexcept -> size_type
    {
        return m_map->size();
    }

    auto find(Key const &key) noexcept -> iterator
    {
        return m_map->find(key);
    }
    auto find(Key const &key) const noexcept -> const_iterator
    {
        return m_map->find(key);
    }
    [[nodiscard]] auto contains(Key const &key) const noexcept -> bool
    {
        return m_map->find(key) != m_map->end();
    }

    auto at(Key const &key) -> T &
    {
        return m_map->at(key);
    }
    auto at(Key const &key) const -> T const &
    {
        return m_map->at(key);
    }

    /** Materialize a child handle.
     *
     * The caller is responsible for the authoritative side of the contract:
     * the canonical key must exist in the parent's child collection and the
     * emplaced handle must refer to the corresponding backend node.
     * Duplicate keys are not replaced silently; use the returned `second`
     * flag to detect them.
     */
    template <typename... Args>
    auto try_emplace(Key const &key, Args &&...args)
        -> std::pair<iterator, bool>
    {
        return m_map->try_emplace(key, std::forward<Args>(args)...);
    }

private:
    template <typename, typename, typename>
    friend class openPMD::Container;

    explicit ChildrenCacheMap(map_type &map) noexcept : m_map(&map)
    {}

    auto map() noexcept -> map_type &
    {
        return *m_map;
    }
    auto map() const noexcept -> map_type const &
    {
        return *m_map;
    }

    map_type *m_map;
};

/** Collection of lazily materialized, type-erased child maps.
 *
 * The maps are projections of a single authoritative child collection, not
 * independent containers. This class only owns the materialized maps and
 * guarantees that structural removal is applied to every one of them.
 *
 * Invariants:
 *  - at most one map exists per (T, Key) pair;
 *  - every materialized map has erase, clear, contains, size, and iteration
 *    callbacks registered;
 *  - erase(key) removes key from every materialized map and leaves no stale
 *    entry behind;
 *  - clear() empties every materialized map.
 *
 * Assertions validate these invariants. In particular, assert_consistent()
 * verifies that every materialized key can be reconstructed from its canonical
 * string representation, since a failed reconstruction would make erase()
 * silently leave an owning handle behind.
 */
class ChildrenCache
{
    struct Entry
    {
        std::shared_ptr<void> storage;
        std::function<void(std::string const &)> erase;
        std::function<void()> clear;
        std::function<bool(std::string const &)> contains;
        std::function<void(std::function<void(std::string const &)> const &)>
            for_each_key;
        std::function<std::size_t()> size;
    };

public:
    using children_map_t =
        std::map<std::string, std::shared_ptr<SharedAttributableData>>;

    ChildrenCache() = default;
    ChildrenCache(ChildrenCache const &) = delete;
    ChildrenCache(ChildrenCache &&) noexcept = default;
    ChildrenCache &operator=(ChildrenCache const &) = delete;
    ChildrenCache &operator=(ChildrenCache &&) noexcept = default;

    /** Get or create the map for (T, Key).
     *
     * Materialization is a cache operation and therefore logically const.
     */
    template <typename T, typename Key>
    auto get_or_create() const -> ChildrenCacheMap<T, Key>
    {
        using map_type = std::map<Key, T>;
        auto const type = std::type_index(typeid(map_type));
        auto it = m_entries.find(type);
        if (it == m_entries.end())
        {
            auto storage = std::make_shared<map_type>();
            auto *map = storage.get();

            Entry entry;
            entry.storage = storage;
            entry.erase = [map](std::string const &key) {
                auto const parsed =
                    children_cache_detail::key_from_string<Key>(key);
                // A key that cannot be represented by this map cannot be
                // contained in it. assert_consistent() validates the
                // round-trip for entries that actually exist.
                if (parsed)
                {
                    map->erase(*parsed);
                }
            };
            entry.clear = [map]() { map->clear(); };
            entry.contains = [map](std::string const &key) {
                auto const parsed =
                    children_cache_detail::key_from_string<Key>(key);
                return parsed.has_value() && map->find(*parsed) != map->end();
            };
            entry.for_each_key =
                [map](std::function<void(std::string const &)> const &functor) {
                    for (auto const &[key, value] : *map)
                    {
                        (void)value;
                        functor(children_cache_detail::key_to_string(key));
                    }
                };
            entry.size = [map]() { return map->size(); };

            it = m_entries.emplace(type, std::move(entry)).first;
            assert_consistent();
        }

        auto *map = static_cast<map_type *>(it->second.storage.get());
        assert(map != nullptr);
        return ChildrenCacheMap<T, Key>(*map);
    }

    template <typename T, typename Key>
    [[nodiscard]] auto find() const -> std::optional<ChildrenCacheMap<T, Key>>
    {
        using map_type = std::map<Key, T>;
        auto const it = m_entries.find(std::type_index(typeid(map_type)));
        if (it == m_entries.end())
        {
            return std::nullopt;
        }
        auto *map = static_cast<map_type *>(it->second.storage.get());
        assert(map != nullptr);
        return ChildrenCacheMap<T, Key>(*map);
    }

    template <typename T, typename Key>
    [[nodiscard]] auto contains() const noexcept -> bool
    {
        return m_entries.find(std::type_index(typeid(std::map<Key, T>))) !=
            m_entries.end();
    }

    children_map_t &children();
    children_map_t const &children() const;

    /** Remove canonicalKey from the authoritative map and every cache. */
    void erase(std::string const &canonicalKey);

    /** Clear the authoritative map and every cache. */
    void clear();

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t num_maps() const noexcept;

    /** Validate internal cache invariants. */
    void assert_consistent() const;

private:
    children_map_t m_children;
    mutable std::map<std::type_index, Entry> m_entries;
};
} // namespace openPMD::internal
