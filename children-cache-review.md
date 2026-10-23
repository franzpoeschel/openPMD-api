# Review: `children-cache` against `custom-hierarchies`

Temporary review notes. This file is committed separately so that the commit can be
reverted independently of the follow-up implementation.

## Findings

### 1. `subhierarchies()` now returns datasets — medium

`src/CustomHierarchy.cpp:155-168`

`res.m_attri->cloneFrom(*this->m_attri)` makes `res` share the same
`GroupMetaData`/`ChildrenCache` as the parent. Calling `res.container_front()`
therefore returns the parent's `CustomHierarchy` map, and `syncContainers()`
materializes *all* backend children into it, including datasets. The `isGroup()`
check only prevents duplicate emplaces; it does not remove already-materialized
datasets.

Reproduced with a group containing a dataset `d` and a subgroup `sub`:

```cpp
auto subs = g.subhierarchies();
subs.contains("d") == true;   // expected false
```

Previously each `ConvertibleContainer` had its own front map, so the result
contained only groups. Any code using `subhierarchies()` to distinguish groups
from datasets is now incorrect.

### 2. Container operations on scalar records abort/throw — medium

`include/openPMD/backend/Container.hpp:300-303`,
`src/backend/BaseRecord.cpp:454,473,560`

`container_front_unchecked()` asserts `objectType.isGroup()` and `group_data()`
throws otherwise. A scalar `BaseRecord` is backed by a dataset, and these methods
are not guarded by `scalar()`/`datasetDefined()`:

- `count("foo")`
- `at("foo")`
- `erase("foo")`

On a scalar record these now hit the assertion (debug builds abort, as verified)
or throw `error::Internal` in release builds. Previously `count`/`erase` returned
`0` and `at` threw `std::out_of_range`.

### 3. Full cache rescan on every access — low/medium

`src/backend/Container.cpp:68-170`

The previous `size_front == size_back` fast path was removed. Every
`container_front()` call for `CustomHierarchy`/`CustomDataset` now scans all
backend children (`O(m log m)`) and then re-verifies the whole front map. Since
`ElementAccessPolicy<CustomHierarchy>` also calls `syncContainers()` on every
iterator dereference, iterating a custom hierarchy with `n` children becomes
`O(n² log n)` instead of the previous near-linear behavior. This is relevant for
large custom hierarchies.

### 4. `noexcept` container accessors can now throw — low

`include/openPMD/backend/ContainerImpl.tpp:36-110`

`begin()`, `end()`, `size()`, `empty()`, etc. are declared `noexcept`, but for
custom types they now call `syncContainers()`, which can throw (`error::Internal`
consistency checks, allocations). If that happens the process terminates instead
of propagating the error. Pre-existing behavior never threw from these
accessors.

## Note

`CustomHierarchy::refresh()` and `Container::swap`/`BaseRecord::swap` are removed
public APIs. If either has out-of-tree users, this is a source-breaking change;
the automatic syncing may make `refresh()` unnecessary, but a deprecation period
would be safer.
