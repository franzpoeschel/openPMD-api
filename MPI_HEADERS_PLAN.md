# Plan: Separate MPI and non-MPI public API headers

## Goal

Allow non-MPI downstream code to include openPMD-api headers from a library
that was built with MPI support, **without pulling in `<mpi.h>`**.

## Target architecture

- `Series_nompi` — full non-MPI base class (`Series_nompi.hpp`, no `mpi.h`).
- `Series_mpi : public Series_nompi` (`Series_mpi.hpp`, includes `mpi.h`) —
  adds only the `MPI_Comm` constructor, **no data members**.
- `Series` — compatibility alias: `Series_mpi` in MPI builds, `Series_nompi`
  otherwise (defined in `Series.hpp`).
- The MPI communicator is type-erased (`std::any`) into
  `internal::SeriesData`, so `sizeof(Series_mpi) == sizeof(Series_nompi)`.
  This keeps objects layout-compatible across the library/consumer boundary
  (needed because `ReadIterations`/`StatefulIterator` hold `Series` by value).
- All internal library code uses `Series_nompi`; the alias is consumer-facing.
- Non-MPI consumers include the `*_nompi.hpp` headers directly.
- `ChunkInfo`, `auxiliary/JSON`, `auxiliary/Filesystem` are split into
  `_nompi`/`_mpi` pairs with compatibility forwarders.
  `IO/AbstractIOHandler.hpp` is fully type-erased (`std::any`) instead of split.

## API-boundary rules

- Base/internal signatures use `Series_nompi` (friends, `retrieveSeries`,
  `Iteration::beginStep`, `StatefulIterator`, `ReadIterations` storage,
  `asInternalCopyOf`).
- Consumer-facing construction/use uses the `Series` alias.
- `HierarchyVisitor::operator()(Series&)` and `debug::printDirty(Series const&)`
  move to `Series_nompi` (the base cannot produce a `Series_mpi`); these are
  untested surfaces.

## Commit sequence

Aliases are introduced as soon as possible so the **unmodified** test suite
acts as the regression oracle for the subsequent refactor. Temporary aliases
(to `Series_nompi`) are permitted while `Series_mpi` does not yet exist; the
final alias always points to the MPI variant in MPI builds.

### Phase 0 — de-risk without touching the public name (tests stay green)

1. **Type-erase the communicator**: `internal::SeriesData`,
   `JSONIOHandlerImpl`, `HDF5IOHandlerImpl`, `ADIOS2IOHandler`:
   `std::optional<MPI_Comm>` → `std::any`. `IO/AbstractIOHandler.hpp` drops
   `<mpi.h>` and the `MPI_Comm` constructor in favor of
   `std::any communicator = {}`; the `.cpp`s `std::any_cast`.

### Phase 1 — `Series` rename / split / alias

1. **Rename `Series` → `Series_nompi`** (library: `include/openPMD`, `src`,
   `src/binding`; `test/` and `examples/` untouched). The base keeps the MPI
   constructor and `<mpi.h>` for now.
2. **Add a temporary `Series` alias** (`using Series = Series_nompi`) via a new
   `Series.hpp`. Tests green — validates that the rename preserved the API.
3. **De-MPI the base + add the MPI subclass**: remove `<mpi.h>`, the MPI
   constructor and `m_communicator` from `Series_nompi.hpp`; make
   `m_series`/`init`/`setData`/`setCommunicator(std::any)` protected; unify
   `rankTable(bool = false)`. New `Series_mpi.hpp` with
   `class Series_mpi : public Series_nompi` adding only the `MPI_Comm`
   constructor and a `static_assert(sizeof(Series_mpi) == sizeof(Series_nompi))`.
4. **Finalize the `Series` alias** (separate commit): `Series.hpp` selects
   `Series_mpi` when `openPMD_HAVE_MPI`, else `Series_nompi`.

### Phase 2 — remaining umbrella-reachable headers

For each: a split commit (base header includes `_nompi`), then a separate
alias/forwarder commit selecting `_mpi` in MPI builds. Tests do not use the
MPI-only free functions, so there is no red window.

- `ChunkInfo.hpp` → `ChunkInfo_nompi.hpp` / `ChunkInfo_mpi.hpp`
  (`host_info::byMethodCollective`).
- `auxiliary/JSON.hpp` → `JSON_nompi.hpp` / `JSON_mpi.hpp` (3-arg
  `json::merge`).
- `auxiliary/Filesystem.hpp` → `Filesystem_nompi.hpp` / `Filesystem_mpi.hpp`
  (`collective_file_read`).

### Phase 3 — build / install / docs / acceptance

- Install the new headers (the whole `include/openPMD` directory is already
  installed, `CMakeLists.txt:1156`).
- Add a CTest target compiling a TU that includes only `*_nompi.hpp` with no
  MPI include path, asserting zero `mpi.h` in the preprocessed output.
- Extend `docs/source/details/mpi.rst`.

## Verification

- Full `ctest` plus examples, unmodified, from the temporary-alias commit
  onward.
- The final alias commit makes MPI tests exercise `Series_mpi` and non-MPI
  builds use `Series_nompi`.
- The no-MPI probe builds with no MPI include path; `SerialIOTests` green.
