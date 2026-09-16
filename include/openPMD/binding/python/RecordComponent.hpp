/* Copyright 2018-2022 Axel Huebl and Franz Poeschel
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
 *
 * The function `bind_container` is based on std_bind.h in pybind11
 * Copyright (c) 2016 Sergey Lyskov and Wenzel Jakob
 *
 * BSD-style license, see pybind11 LICENSE file.
 */

#pragma once

#include "openPMD/RecordComponent.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <utility>

namespace py = pybind11;
using namespace openPMD;

/*
 * A lazily-evaluating handle to a chunk load/store operation.
 *
 * Created by `Record_Component.__getitem__`. It captures the record component,
 * the resolved offset/extent and the shape that a numpy array covering the
 * selection would have (with integer-indexed axes already dropped, cf.
 * `flatten`).
 *
 * No I/O happens at construction time. I/O happens on first access:
 *   - as a buffer (PEP 3118, via `def_buffer`) when numpy or `memoryview`
 *     consumes the object, e.g. as the source of an assignment or
 *     `np.asarray(rc[...])`
 *   - via the `__array__` protocol (preferred by numpy)
 *   - via `.load()` returning a numpy array owning the loaded data
 *
 * The object keeps the surrounding `Series` alive for as long as it exists,
 * and the loaded buffer is cached, so the returned array stays valid even
 * after the record component / series is out of scope.
 *
 * When such an object is used on the *right hand side* of a numpy assignment,
 * numpy converts it (via `__array__` / the buffer protocol) to a numpy array
 * *before* the assignment target is touched, so the load is performed under
 * openPMD's control immediately.
 */
class PythonLazyLoadStoreChunk
{
public:
    PythonLazyLoadStoreChunk(
        ConfigureLoadStore operationBuilder, std::vector<py::ssize_t> shape);

    PythonLazyLoadStoreChunk(
        RecordComponent &rc,
        Offset offset,
        Extent extent,
        std::vector<py::ssize_t> shape);

    auto const &shape() const;

    auto const &operationBuilder() const;

    auto &operationBuilder();

    /** The datatype of the underlying record component */
    auto getDatatype() const -> Datatype;

    /**
     * Perform the load (if not yet done) and return a numpy array owning the
     * loaded data.
     *
     * The result is cached; subsequent calls (including through the buffer
     * protocol / `__array__`) return the same array.
     */
    auto load() -> py::array &;

    void enqueueLoad();

    /** Buffer protocol export: run the load on first access.
     *
     * The returned `py::buffer_info` points into the (cached) numpy array,
     * which is kept alive by the exporting object itself (the Python object
     * wrapping `*this`), so the memory stays valid for the memoryview's whole
     * lifetime.
     */
    py::buffer_info getBuffer();

    /**
     * Store data from `buffer` into the record component at this chunk's
     * offset/extent.
     *
     * @param buffer Any PEP 3118 buffer (numpy array, memoryview, array.array,
     *               ...). The buffer's shape must match the selection's shape;
     *               a (possibly strided) sub-cuboid view is handled with a
     *               memory selection.
     */
    void store(py::buffer const &buffer);

    /**
     * Load this chunk's data directly into `buffer`.
     *
     * Unlike `.load()`, which allocates a fresh numpy array and copies the
     * loaded data into it, `.into()` loads the dataset chunk *directly* into
     * a caller-provided buffer through a single backend operation:
     *
     *   - a contiguous buffer (or a whole owning buffer) uses the ordinary
     *     contiguous load path with no intermediate copy;
     *   - a (possibly non-contiguous in its own shape) sub-cuboid view of a
     *     larger buffer uses a *memory selection*, loading the chunk directly
     *     into that sub-region with one `READ_DATASET` and no intermediate.
     *
     * Unlike the `load_chunk` pass-through API, the load is performed
     * synchronously: after this call returns, the buffer is fully populated
     * (an automatic flush runs during the evaluation), matching the semantics
     * of `.load()`.
     *
     * The buffer's shape must match this chunk's selection shape. The buffer
     * is returned, so the call can be chained:
     *
     *   rc[:, :, :] .into(my_buffer)          # contiguous, zero-copy
     *   rc[2:4, 2:4, 2:4] .into(dst[2:4,...]) # memory selection
     *
     * @param buffer_obj Any writable PEP 3118 buffer (numpy array, memoryview,
     *                   array.array, ...).
     * @return The `buffer` itself.
     */
    py::object into(py::object const &buffer_obj);

private:
    auto doLoad(bool do_flush) -> py::array &;

    auto strides_from_extent() -> std::vector<py::ssize_t>;

    ConfigureLoadStore m_operationBuilder;
    std::vector<py::ssize_t> m_shape;
    std::optional<py::array> m_cache;
};

inline void load_chunk(
    RecordComponent &r,
    py::buffer &buffer,
    Offset const &offset,
    Extent const &extent);

/*
 * Definitions for these functions `store_chunk` and the lazy slicing helpers
 * found in python/RecordComponent.cpp.
 * No need to pull them here, as they are not templates.
 */
void store_chunk(RecordComponent &r, py::array &a, py::tuple const &slices);

/** Create a lazily-evaluating load/store chunk handle for the given slices.
 *
 * Unlike `load_chunk`, this does not perform any I/O. The returned object
 * implements the buffer protocol and `__array__`, so numpy converts it (and
 * thereby triggers the actual load) transparently.
 */
auto load_chunk_lazy(RecordComponent &self, py::tuple const &slices)
    -> py::object;
auto load_chunk_lazy_slice(RecordComponent &self, py::slice const &slice_obj)
    -> py::object;
auto load_chunk_lazy_int(RecordComponent &self, py::int_ const &slice_obj)
    -> py::object;

/** Store `value` (a numpy array, generic buffer or another lazy chunk handle)
 * into the record component at the selection described by `slices`.
 *
 * If `value` is a lazy chunk handle, it is resolved first (performing the
 * load through openPMD) and the resulting array is stored. Otherwise the value
 * is stored directly, deriving a memory selection from strided views.
 */
void store_chunk_object(
    RecordComponent &r, py::tuple const &slices, py::object value);
void store_chunk_object_slice(
    RecordComponent &r, py::slice const &slice_obj, py::object value);
void store_chunk_object_int(
    RecordComponent &r, py::int_ const &slice_obj, py::object value);

namespace docstring
{
constexpr static char const *is_scalar = R"docstr(
Returns true if this record only contains a single component.
)docstr";
}

template <typename Class>
Class &&addRecordComponentSetGet(Class &&class_)
{
    // TODO if we also want to support scalar arrays, we have to switch
    //      py::array for py::buffer as in Attributable
    //      https://github.com/pybind/pybind11/pull/1537

    // slicing protocol
    class_
        .def(
            "__getitem__",
            [](RecordComponent &self, py::tuple const &slices) {
                return load_chunk_lazy(self, slices);
            },
            py::keep_alive<0, 1>(),
            py::arg("tuple of index slices"))
        .def(
            "__getitem__",
            [](RecordComponent &self, py::slice const &slice_obj) {
                return load_chunk_lazy_slice(self, slice_obj);
            },
            py::keep_alive<0, 1>(),
            py::arg("slice"))
        .def(
            "__getitem__",
            [](RecordComponent &self, py::int_ const &slice_obj) {
                return load_chunk_lazy_int(self, slice_obj);
            },
            py::keep_alive<0, 1>(),
            py::arg("axis index"))
        .def(
            "__iter__",
            [](py::object const &self) {
                // Repeated lazy single-row loads are done only when the user
                // actually iterates; each row is loaded independently.
                return self.attr("load")().attr("__iter__")();
            })

        .def(
            "__setitem__",
            [](RecordComponent &r, py::tuple const &slices, py::array &a) {
                // store_chunk() transparently falls back to the ordinary
                // contiguous path when no memory selection is needed (e.g.
                // contiguous own-buffer arrays).
                store_chunk(r, a, slices);
            },
            py::arg("tuple of index slices"),
            py::arg("array with values to assign"))
        .def(
            "__setitem__",
            [](RecordComponent &r, py::slice const &slice_obj, py::array &a) {
                auto const slices = py::make_tuple(slice_obj);
                store_chunk(r, a, slices);
            },
            py::arg("slice"),
            py::arg("array with values to assign"))
        .def(
            "__setitem__",
            [](RecordComponent &r, py::int_ const &slice_obj, py::array &a) {
                auto const slices = py::make_tuple(slice_obj);
                store_chunk(r, a, slices);
            },
            py::arg("axis index"),
            py::arg("array with values to assign"))
        // RHS may be another lazy chunk handle or a generic buffer object:
        // resolve it (performing the load through openPMD) and then store.
        .def(
            "__setitem__",
            [](RecordComponent &r, py::tuple const &slices, py::object value) {
                store_chunk_object(r, slices, std::move(value));
            },
            py::arg("tuple of index slices"),
            py::arg("values to assign"))
        .def(
            "__setitem__",
            [](RecordComponent &r,
               py::slice const &slice_obj,
               py::object value) {
                store_chunk_object_slice(r, slice_obj, std::move(value));
            },
            py::arg("slice"),
            py::arg("values to assign"))
        .def(
            "__setitem__",
            [](RecordComponent &r,
               py::int_ const &slice_obj,
               py::object value) {
                store_chunk_object_int(r, slice_obj, std::move(value));
            },
            py::arg("axis index"),
            py::arg("values to assign"));
    return std::forward<Class>(class_);
}
