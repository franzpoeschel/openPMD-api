# Review follow-up plan

All planned action items have been implemented and validated.

## Action items

### 1. Replace SFINAE with `if constexpr` in `key_from_string_or_throw` [done]

File: `include/openPMD/backend/ChildrenCache.hpp`

- Remove the `KeyFromStringOrThrow` primary template, the `enable_if` partial
  specialization, and the `std::string` full specialization.
- Implement the dispatch directly in `key_from_string_or_throw`:
  - `std::string` returns `std::string const &`
  - integral keys return the parsed `Key` by value
  - unsupported keys use `auxiliary::dependent_false_v`
- For integral keys, do not return `*parsed` directly: `decltype(auto)` would
  deduce `Key &` and return a reference to the local `optional`. Return
  `static_cast<Key>(*parsed)` (or an equivalent value).

### 2. Apply the same simplification to `key_to_string` [done]

File: `include/openPMD/backend/ChildrenCache.hpp`

- `KeyToStringTraits` currently uses the same template/`enable_if`
  specialization pattern.
- Replace it with one `if constexpr` implementation:
  - `std::string` returns `std::string const &`
  - integral keys return `std::string` by value
  - unsupported keys use `auxiliary::dependent_false_v`
- This was not explicitly requested; include it only if confirmed.

## Validation

- Rebuild `CoreTests`, `AuxiliaryTests`, and `SerialIOTests`.
- Run:
  - `./build/bin/CoreTests`
  - `./build/bin/AuxiliaryTests`
  - the relevant SerialIO subset (`write_and_read_many_iterations`,
    `groupbased_read_write`, `filebased_write_test`).
- Confirm no key-conversion behavior changes: canonical integral keys still
  parse, non-canonical forms remain rejected, and `std::string` keys remain
  zero-copy where possible.

### 3. Forbid `key_from_string<std::string>()`

File: `include/openPMD/backend/ChildrenCache.hpp`,
`include/openPMD/backend/ChildView.hpp`

- [x] Add a `static_assert` in the `std::string` branch of `key_from_string`
  so that instantiating it is a compile error.
- [x] Use `if constexpr` directly in the affected call sites:
  - `ChildrenCache` entry erase/contains callbacks
  - `ChildView::matches`
  - `ChildView::sync` (already guarded by `non_string`)
- [x] Avoid instantiating `key_from_string<Key>` for string-keyed maps.
- [x] Validate Core, Auxiliary, and SerialIO subset.

### 4. Factor `ProjectedChildIterator` members into `struct Members` [done]

File: `include/openPMD/backend/ChildView.hpp`

- [x] Introduce a Const-independent `Members` struct holding the iterator state:
  - `ChildrenCache *cache`
  - the authoritative children iterator
  - the authoritative children end iterator
  - `ChildFilter filter`
  - `bool access_policy`
- [x] Ensure `Members` does not depend on the `Const` template parameter. Store
  the non-const authoritative iterator type in `Members`; `Const` should only
  affect the exposed `reference`, `pointer`, and the typed-cache iterator
  conversion at dereference.
- [x] Make the non-const-to-const conversion constructor copy the `Members`
  aggregate directly instead of listing each field manually.
- [x] Keep iterator behavior and const-correctness of returned references
  unchanged.

### 5. Assert that non-string/materialized views do not use access policies [done]

File: `include/openPMD/backend/ChildView.hpp`

- [x] Access policies are only meaningful for projected string-keyed views,
  not for the materialized/non-string (`Series::iterations`) path.
- [x] Add assertions at the access-policy entry points so an access policy
  cannot silently be requested for a non-string view, e.g.:
  - `assert(!non_string)` in `set_access_policy`
  - assert/document that materialized-view construction does not treat the
    default policy flag as an active request
- [x] Ensure the existing `CustomHierarchy::read` path, which disables access
  policies on a string-keyed view, still conforms.

### 6. Wrap nullable `ChildView` members in `std::optional` [done]

File: `include/openPMD/backend/ChildView.hpp`,
`include/openPMD/backend/Container.hpp`

- [x] `ChildView::m_filter` may be null (const views currently pass a null
  filter). Store it as `std::optional<std::reference_wrapper<ChildFilter const>>`
  or equivalent, rather than a raw pointer.
- [x] `ChildView::m_create_child` may be null (const views do not allow child
  creation). Store it as `std::optional<CreateChild>` or equivalent.
- [x] Assert/throw explicitly at use sites instead of relying on raw-pointer
  null checks where the operation is unsupported.
- [x] Keep view construction lightweight; optionals of pointers/trivial values
  should not introduce allocations.
- [x] Update `Container::container()` and const-view construction accordingly.
