/* Copyright 2018-2025 Axel Huebl, Franz Poeschel, Luca Fedeli
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
#include <limits>
#include <pybind11/detail/common.h>
#include <pybind11/gil.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>

#include "openPMD/Dataset.hpp"
#include "openPMD/Datatype.hpp"
#include "openPMD/DatatypeHelpers.hpp"
#include "openPMD/Error.hpp"
#include "openPMD/LoadStoreChunk.hpp"
#include "openPMD/RecordComponent.hpp"
#include "openPMD/Series.hpp"
#include "openPMD/backend/BaseRecordComponent.hpp"

#include "openPMD/binding/python/Common.hpp"
#include "openPMD/binding/python/Container.H"
#include "openPMD/binding/python/Numpy.hpp"
#include "openPMD/binding/python/Pickle.hpp"
#include "openPMD/binding/python/RecordComponent.hpp"

#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

/** Convert a py::tuple of py::slices to Offset & Extent
 *
 * https://docs.scipy.org/doc/numpy-1.15.0/reference/arrays.indexing.html
 * https://github.com/numpy/numpy/blob/v1.16.1/numpy/core/src/multiarray/mapping.c#L348-L375
 */
inline std::tuple<Offset, Extent, std::vector<bool>> parseTupleSlices(
    uint8_t const ndim, Extent const &full_extent, py::tuple const &slices)
{
    uint8_t const numSlices = py::len(slices);

    Offset offset(ndim, 0u);
    Extent extent(ndim, 1u);
    std::vector<bool> flatten(ndim, false);
    int16_t curAxis = -1;

    int16_t posEllipsis = -1;
    for (uint8_t i = 0u; i < numSlices; ++i)
    {
        ++curAxis;

        if (i >= ndim && posEllipsis == -1 && slices[i].ptr() != Py_Ellipsis)
            throw py::index_error(
                "too many indices for dimension of record component!");

        if (slices[i].ptr() == Py_Ellipsis)
        {
            // only allowed once
            if (posEllipsis != -1)
                throw py::index_error(
                    "an index can only have a single ellipsis ('...')");
            posEllipsis = curAxis;

            // might be omitted if all other indices are given as well
            if (numSlices == ndim + 1)
            {
                --curAxis;
                continue;
            }

            // how many slices were given after the ellipsis
            uint8_t const numSlicesAfterEllipsis =
                numSlices - uint8_t(posEllipsis) - 1u;
            // how many slices does the ellipsis represent
            uint8_t const numSlicesEllipsis = numSlices -
                uint8_t(posEllipsis) // slices before
                - numSlicesAfterEllipsis; // slices after

            // fill ellipsis indices
            // note: if enough further indices are given, the ellipsis
            //       might stand for no axis: valid and ignored
            for (; curAxis < posEllipsis + int16_t(numSlicesEllipsis);
                 ++curAxis)
            {
                offset.at(curAxis) = 0;
                extent.at(curAxis) = full_extent.at(curAxis);
            }
            --curAxis;

            continue;
        }

        if (PySlice_Check(slices[i].ptr()))
        {
            py::slice slice = py::cast<py::slice>(slices[i]);

            size_t start, stop, step, slicelength;
            auto mocked_extent = full_extent.at(curAxis);
            // py::ssize_t is a signed type, so we will need to use another
            // magic number for JOINED_DIMENSION in this computation, since the
            // C++ API's JOINED_DIMENSION would be interpreted as a negative
            // index
            bool undo_mocked_extent = false;
            constexpr auto PYTHON_JOINED_DIMENSION =
                std::numeric_limits<py::ssize_t>::max() - 1;
            if (mocked_extent == Dataset::JOINED_DIMENSION)
            {
                undo_mocked_extent = true;
                mocked_extent = PYTHON_JOINED_DIMENSION;
            }
            if (!slice.compute(
                    mocked_extent, &start, &stop, &step, &slicelength))
                throw py::error_already_set();

            if (undo_mocked_extent)
            {
                // do the same calculation again, but with another global extent
                // (that is not smaller than the previous in order to avoid
                // cutting off the range)
                // this is to avoid the unlikely case
                // that the mocked alternative value is actually the intended
                // one
                size_t start2, stop2, step2, slicelength2;
                if (!slice.compute(
                        mocked_extent + 1,
                        &start2,
                        &stop2,
                        &step2,
                        &slicelength2))
                    throw py::error_already_set();
                if (slicelength == slicelength2)
                {
                    // slicelength was given as an absolute value and
                    // accidentally hit our mocked value
                    // --> keep that value
                    undo_mocked_extent = false;
                }
            }

            // TODO PySlice_AdjustIndices: Python 3.6.1+
            //      Adjust start/end slice indices assuming a sequence of the
            //      specified length. Out of bounds indices are clipped in a
            //      manner consistent with the handling of normal slices.
            // slicelength = PySlice_AdjustIndices(full_extent[curAxis],
            // (ssize_t*)&start, (ssize_t*)&stop, step);

            if (step != 1u)
                throw py::index_error(
                    "strides in selection are inefficient, not implemented!");

            // verified for size later in C++ API
            offset.at(curAxis) = start;
            extent.at(curAxis) =
                undo_mocked_extent && slicelength == PYTHON_JOINED_DIMENSION
                ? Dataset::JOINED_DIMENSION
                : slicelength; // stop - start;

            continue;
        }

        try
        {
            auto const index = py::cast<std::int64_t>(slices[i]);

            if (index < 0)
                offset.at(curAxis) = full_extent.at(curAxis) + index;
            else
                offset.at(curAxis) = index;

            extent.at(curAxis) = 1;
            flatten.at(curAxis) = true; // indices flatten the dimension

            if (offset.at(curAxis) >= full_extent.at(curAxis))
                throw py::index_error(
                    std::string("index ") + std::to_string(offset.at(curAxis)) +
                    std::string(" is out of bounds for axis ") +
                    std::to_string(i) + std::string(" with size ") +
                    std::to_string(full_extent.at(curAxis)));

            continue;
        }
        // NOLINTNEXTLINE(bugprone-empty-catch)
        catch (const py::cast_error &e)
        {
            // not an index
        }

        if (slices[i].ptr() == Py_None)
        {
            // py::none newaxis = py::cast< py::none >( slices[i] );;
            throw py::index_error("None (newaxis) not implemented!");

            // continue;
        }

        // if we get here, the last slice type was not successfully processed
        --curAxis;
        throw py::index_error(
            std::string("unknown index type passed: ") +
            py::str(slices[i]).cast<std::string>());
    }

    // fill omitted higher indices with "select all"
    for (++curAxis; curAxis < int16_t(ndim); ++curAxis)
    {
        extent.at(curAxis) = full_extent.at(curAxis);
    }

    return std::make_tuple(offset, extent, flatten);
}

inline std::tuple<Offset, Extent, std::vector<bool>> parseJoinedTupleSlices(
    uint8_t const ndim,
    Extent const &full_extent,
    py::tuple const &slices,
    size_t joined_dim,
    py::array const &a)
{

    std::vector<bool> flatten;
    Offset offset;
    Extent extent;
    std::tie(offset, extent, flatten) =
        parseTupleSlices(ndim, full_extent, slices);
    for (size_t i = 0; i < ndim; ++i)
    {
        if (offset.at(i) != 0)
        {
            throw std::runtime_error(
                "Joined array: Cannot use non-zero offset in store_chunk "
                "(offset[" +
                std::to_string(i) + "] = " + std::to_string(offset[i]) + ").");
        }
        if (flatten.at(i))
        {
            throw std::runtime_error(
                "Flattened slices unimplemented for joined arrays.");
        }

        if (i == joined_dim)
        {
            if (extent.at(i) == 0 || extent.at(i) == Dataset::JOINED_DIMENSION)
            {
                extent[i] = a.shape()[i];
            }
        }
        else
        {
            if (extent.at(i) != full_extent.at(i))
            {
                throw std::runtime_error(
                    "Joined array: Must use full extent in store_chunk for "
                    "non-joined dimension "
                    "(local_extent[" +
                    std::to_string(i) + "] = " + std::to_string(extent[i]) +
                    " != global_extent[" + std::to_string(i) +
                    "] = " + std::to_string(full_extent[i]) + ").");
            }
        }
    }
    offset.clear();
    return std::make_tuple(offset, extent, flatten);
}

/** Check a buffer is a contiguous buffer
 *
 * Required are contiguous buffers for store and load, unless a memory
 * selection is used to address a sub-region of the (contiguous) memory block.
 *
 * - not strided with paddings
 * - not a view in another buffer that results in striding
 */
inline void check_buffer_is_contiguous(py::buffer_info const &info)
{
    bool isContiguous = true;
    py::ssize_t expected = info.itemsize;
    for (py::ssize_t d = info.ndim - 1; d >= 0; --d)
    {
        if (info.strides[d] != expected)
        {
            isContiguous = false;
            break;
        }
        expected *= info.shape[d];
    }

    if (!isContiguous)
        throw py::index_error(
            "strides in chunk are inefficient, not implemented!");
}

namespace
{

struct StoreChunkFromPythonArray
{
    template <typename T>
    static void call(
        ConfigureLoadStore operationBuilder,
        py::object owning_handle_for_array,
        void *data,
        std::optional<MemorySelection> memorySelection)
    {
        // here, we store an owning handle in the lambda capture so that
        // temporary and lost-scope variables stay alive until we flush
        // note: this does not yet prevent the user, as in C++, to build
        // a race condition by manipulating the data that was passed
        std::shared_ptr<T> shared(
            (T *)data,
            [owning_handle = std::make_optional(
                 std::move(owning_handle_for_array))](T *) mutable {
                py::gil_scoped_acquire need_the_gil_for_this;
                owning_handle.reset();
            });
        auto config = operationBuilder.withSharedPtr(std::move(shared));
        if (memorySelection.has_value())
        {
            config.memorySelection(std::move(*memorySelection));
        }
        config.unsafeNoAutomaticFlush().store().get();
    }

    static constexpr char const *errorMsg = "store_chunk()";
};
struct LoadChunkIntoPythonArray
{
    template <typename T>
    static void call(
        RecordComponent &r,
        py::object owning_handle,
        void *data,
        Offset const &offset,
        Extent const &extent,
        std::optional<MemorySelection> memorySelection)
    {
        // here, we store an owning handle in the lambda capture so that
        // temporary and lost-scope variables stay alive until we flush
        // note: this does not yet prevent the user, as in C++, to build
        // a race condition by manipulating the data that was passed
        std::shared_ptr<T> shared(
            (T *)data,
            [owning_handle =
                 std::make_optional(std::move(owning_handle))](T *) mutable {
                py::gil_scoped_acquire need_the_gil_for_this;
                owning_handle.reset();
            });
        auto config =
            r.prepareLoadStore().offset(offset).extent(extent).withSharedPtr(
                std::move(shared));
        if (memorySelection.has_value())
        {
            config.memorySelection(std::move(*memorySelection));
        }
        config.unsafeNoAutomaticFlush().load().get();
    }

    static constexpr char const *errorMsg = "load_chunk()";
};

/*
 * Synchronous, auto-flushing variant of the load-into-buffer used by the
 * lazy chunk's `.into()` method.
 *
 * Unlike LoadChunkIntoPythonArray (which defers the flush via
 * unsafeNoAutomaticFlush() and requires an explicit series flush afterwards),
 * this evaluates the returned DeferredComputation immediately, so the buffer
 * is fully populated when `.into()` returns -- matching the semantics of the
 * allocating `.load()` variant.
 */
struct LoadChunkIntoBufferSynchronously
{
    template <typename T>
    static void call(
        ConfigureLoadStore operationBuilder,
        py::object owning_handle_for_array,
        void *data,
        std::optional<MemorySelection> memorySelection)
    {
        std::shared_ptr<T> shared(
            (T *)data,
            [owning_handle = std::make_optional(
                 std::move(owning_handle_for_array))](T *) mutable {
                py::gil_scoped_acquire need_the_gil_for_this;
                owning_handle.reset();
            });
        auto config = operationBuilder.withSharedPtr(std::move(shared));
        if (memorySelection.has_value())
        {
            config.memorySelection(std::move(*memorySelection));
        }
        // evaluate immediately (runs the automatic flush)
        config.load()();
    }

    static constexpr char const *errorMsg = "Load_Store_Chunk.into()";
};
} // namespace

/*
 * Defined further below; forward declarations so the store/load entry points
 * above can use them.
 */
inline std::optional<MemorySelection>
derive_memory_selection(py::object const &obj, py::buffer_info const &info);
inline py::object deepest_owner(py::object const &obj);

/** Store Chunk
 *
 * Called with offset and extent that are already in the record component's
 * dimension.
 *
 * Size checks of the requested chunk (spanned data is in valid bounds)
 * will be performed at C++ API part in RecordComponent::storeChunk .
 *
 * If `memorySelection` is set, the data is read from the given sub-region of
 * the (deepest) memory block behind `a`; otherwise `a` must be a contiguous
 * buffer covering the selection.
 */
inline void store_chunk(
    RecordComponent &r,
    py::array &a,
    Offset const &offset,
    Extent const &extent,
    std::vector<bool> const &flatten,
    std::optional<MemorySelection> memorySelection = std::nullopt)
{
    py::buffer_info const info = a.request(/* writable = */ true);

    // @todo keep locked until flush() is performed
    // a.flags.writable = false;
    // a.flags.owndata = false;

    // verify offset + extend fit in dataset extent

    //   some one-size dimensions might be flattended in our r due to selections
    //   by index
    size_t const numFlattenDims =
        std::count(flatten.begin(), flatten.end(), true);
    auto const r_extent = r.getExtent();
    auto const &s_extent(extent); // selected extent in r
    std::vector<std::uint64_t> r_shape(r_extent.size() - numFlattenDims);
    std::vector<std::uint64_t> s_shape(s_extent.size() - numFlattenDims);
    auto maskIt = flatten.begin();
    std::copy_if(
        std::begin(r_extent),
        std::end(r_extent),
        std::begin(r_shape),
        [&maskIt](std::uint64_t) { return !*(maskIt++); });
    maskIt = flatten.begin();
    std::copy_if(
        std::begin(s_extent),
        std::end(s_extent),
        std::begin(s_shape),
        [&maskIt](std::uint64_t) { return !*(maskIt++); });

    //   verify shape and extent
    if (size_t(info.ndim) != r_shape.size())
        throw py::index_error(
            std::string("dimension of chunk (") + std::to_string(info.ndim) +
            std::string(
                "D) does not fit dimension of selection "
                "in record component (") +
            std::to_string(r_shape.size()) + std::string("D)"));

    if (auto joined_dim = r.joinedDimension(); joined_dim.has_value())
    {
        for (py::ssize_t d = 0; d < info.ndim; ++d)
        {
            // selection causes overflow of r
            if (d != py::ssize_t(*joined_dim) && extent.at(d) != r_shape.at(d))
                throw py::index_error(
                    std::string("selection for axis ") + std::to_string(d) +
                    " of record component with joined dimension " +
                    std::to_string(*joined_dim) +
                    " must be equivalent to its global extent " +
                    std::to_string(extent.at(d)) + ", but was " +
                    std::to_string(r_shape.at(d)) + ".");
            // underflow of selection in r for given a
            if (s_shape.at(d) != std::uint64_t(info.shape[d]))
                throw py::index_error(
                    std::string("size of chunk (") +
                    std::to_string(info.shape[d]) + std::string(") for axis ") +
                    std::to_string(d) +
                    std::string(" does not match selection ") +
                    std::string("size in record component (") +
                    std::to_string(s_extent.at(d)) + std::string(")"));
        }
    }
    else
    {
        for (auto d = 0; d < info.ndim; ++d)
        {
            // selection causes overflow of r
            if (offset.at(d) + extent.at(d) > r_shape.at(d))
                throw py::index_error(
                    std::string("slice ") + std::to_string(offset.at(d)) +
                    std::string(":") + std::to_string(extent.at(d)) +
                    std::string(" is out of bounds for axis ") +
                    std::to_string(d) + std::string(" with size ") +
                    std::to_string(r_shape.at(d)));
            // underflow of selection in r for given a
            if (s_shape.at(d) != std::uint64_t(info.shape[d]))
                throw py::index_error(
                    std::string("size of chunk (") +
                    std::to_string(info.shape[d]) + std::string(") for axis ") +
                    std::to_string(d) +
                    std::string(" does not match selection ") +
                    std::string("size in record component (") +
                    std::to_string(s_extent.at(d)) + std::string(")"));
        }
    }

    if (!memorySelection.has_value())
    {
        check_buffer_is_contiguous(info);
    }

    // datatype check: the buffer's PEP 3118 format string must map to the
    // record component's datatype
    Datatype const buffer_dtype = dtype_from_bufferformat(info.format);
    if (buffer_dtype != r.getDatatype())
    {
        std::stringstream err;
        err << "Attempting store from Python buffer of type '" << buffer_dtype
            << "' into Record Component of type '" << r.getDatatype() << "'.";
        throw error::WrongAPIUsage(err.str());
    }

    py::object owner_obj = memorySelection.has_value()
        ? deepest_owner(py::reinterpret_borrow<py::object>(a))
        : py::reinterpret_borrow<py::object>(a);
    py::buffer owner_buf = py::cast<py::buffer>(owner_obj);
    auto owner_info = owner_buf.request(/* writable = */ true);
    switchDatasetType<StoreChunkFromPythonArray>(
        r.getDatatype(),
        r.prepareLoadStore().offset(offset).extent(extent),
        owner_obj,
        owner_info.ptr,
        std::move(memorySelection));
}

/** Store Chunk
 *
 * Called with a py::tuple of slices and a py::array.
 *
 * If the RHS array is a (non-flattened) view of a larger contiguous buffer, a
 * memory selection is derived and the store happens through a single
 * prepareLoadStore().memorySelection().store() operation. Otherwise (owning or
 * flattened selection), the ordinary contiguous path is used.
 */
inline void
store_chunk(RecordComponent &r, py::array &a, py::tuple const &slices)
{
    uint8_t ndim = r.getDimensionality();
    auto const full_extent = r.getExtent();

    Offset offset;
    Extent extent;
    std::vector<bool> flatten;
    if (auto joined_dimension = r.joinedDimension();
        joined_dimension.has_value())
    {
        std::tie(offset, extent, flatten) = parseJoinedTupleSlices(
            ndim, full_extent, slices, *joined_dimension, a);
    }
    else
    {
        std::tie(offset, extent, flatten) =
            parseTupleSlices(ndim, full_extent, slices);
    }

    std::optional<MemorySelection> memorySelection;
    if (std::count(flatten.begin(), flatten.end(), true) == 0)
    {
        // No axis was flattened by integer indexing: a memory selection may
        // apply. derive_memory_selection() returns nullopt for whole /
        // owning / contiguous arrays, in which case the contiguous path
        // (which validates contiguity and throws for genuinely strided data,
        // as before) is taken.
        auto info = a.request(/* writable = */ false);
        memorySelection = derive_memory_selection(
            py::reinterpret_borrow<py::object>(a), info);
    }

    store_chunk(r, a, offset, extent, flatten, std::move(memorySelection));
}

/** Derive an openPMD MemorySelection from a (view of a) buffer.
 *
 * openPMD memory selections describe a sub-region of a contiguous, row-major
 * memory buffer by its offset (in elements) within that buffer and the full
 * shape of the buffer (cf. ParallelIOTest.cpp and the ADIOS2 backend's
 * `SetMemorySelection`, which expects `{memoryStart, memoryCount}` where
 * `memoryCount` is the shape of the contiguous memory block that the data
 * pointer refers to).
 *
 * Multidimensional slicing of a row-major buffer along all axes produces a
 * view whose strides are the *same* as the parent buffer's strides. From such
 * a view we can recover:
 *   - the origin of the contiguous memory block (the deepest owner object),
 *   - the full shape of the memory block (from the view's strides and the
 *     block's element count),
 *   - the per-axis offset at which the view starts (by decomposing the data
 *     pointer delta against the block's row-major strides).
 *
 * Works on any PEP 3118 buffer (numpy arrays, memoryviews, `array.array`,
 * raw bytes, custom exporters) via its `py::buffer_info`; there is no
 * dependency on numpy semantics such as `.base`.
 *
 * Supported: sub-cuboid views such as `write_buffer[2:4, 2:4, 2:4]`, i.e.
 * `rho[0:2, 0:2, 0:2] = write_buffer[2:4, 2:4, 2:4]`.
 *
 * Unsupported and rejected (matching the pre-existing error, so existing tests
 * keep passing): arbitrary strided views such as `[:, ::2]`, dropped axes /
 * integer-indexed views where the block shape cannot be reconstructed.
 *
 * @return The memory selection {offset, extent}, or std::nullopt if the view
 *         covers the whole memory block (no selection needed).
 */
inline std::optional<MemorySelection>
derive_memory_selection(py::object const &obj, py::buffer_info const &info)
{
    py::ssize_t const ndim = info.ndim;
    if (ndim == 0)
    {
        return std::nullopt;
    }
    py::ssize_t const itemsize = info.itemsize;

    /*
     * If the buffer is contiguous in its own shape, it covers (a contiguous
     * sub-block of) the memory block and no memory selection is needed: go
     * down the contiguous fast path (which performs the proper
     * dimensionality/shape checks, e.g. rejecting `np.ones((43,13,4))` for a
     * 2-D record component).
     */
    {
        py::ssize_t expected = itemsize;
        bool contiguous = true;
        for (py::ssize_t d = ndim - 1; d >= 0; --d)
        {
            if (info.strides[d] != expected)
            {
                contiguous = false;
                break;
            }
            expected *= info.shape[d];
        }
        if (contiguous)
        {
            return std::nullopt;
        }
    }

    // Walk to the deepest owner of the memory block (the object that actually
    // owns the contiguous backing memory). For numpy arrays NumPy collapses
    // nested views to a flat owner array; for memoryviews / generic buffers we
    // follow the buffer's `.obj` chain.
    py::object owner_obj = deepest_owner(obj);
    py::buffer owner_buf;
    try
    {
        owner_buf = py::cast<py::buffer>(owner_obj);
    }
    catch (py::cast_error const &)
    {
        throw py::index_error(
            "strides in chunk are inefficient, not implemented!");
    }
    auto owner_info = owner_buf.request();
    if (owner_info.itemsize != itemsize)
    {
        throw py::index_error(
            "strides in chunk are inefficient, not implemented!");
    }
    std::size_t const block_elems = static_cast<std::size_t>(owner_info.size);
    void *const origin = owner_info.ptr;

    /*
     * Reconstruct the memory block shape from the view's strides.
     *
     * For a C-contiguous block of shape B, stride[i] = prod_{j>i} B[j].
     * If the view retains all axes (basic slicing of every axis), its strides
     * equal the block's strides, so:
     *   B[i] = stride[i] / stride[i+1]        (i < ndim-1)
     *   B[ndim-1] = block_elems / prod_{i<ndim-1} B[i]
     */
    std::vector<std::uint64_t> view_strides_elem(ndim);
    for (py::ssize_t d = 0; d < ndim; ++d)
    {
        view_strides_elem[d] =
            std::uint64_t(info.strides[d]) / std::uint64_t(itemsize);
        if (view_strides_elem[d] == 0 && info.shape[d] > 1)
        {
            // zero-sized / broadcast-like stride: not a sub-cuboid
            throw py::index_error(
                "strides in chunk are inefficient, not implemented!");
        }
    }
    std::vector<std::uint64_t> block_shape(ndim, 1u);
    {
        std::uint64_t prod = 1u;
        for (py::ssize_t d = 0; d + 1 < ndim; ++d)
        {
            if (view_strides_elem[d] % view_strides_elem[d + 1] != 0 ||
                view_strides_elem[d] < view_strides_elem[d + 1])
            {
                throw py::index_error(
                    "strides in chunk are inefficient, not implemented!");
            }
            block_shape[d] = view_strides_elem[d] / view_strides_elem[d + 1];
            prod *= block_shape[d];
        }
        if (prod == 0 || block_elems % prod != 0)
        {
            throw py::index_error(
                "strides in chunk are inefficient, not implemented!");
        }
        // The final dimension size must be consistent with the view's own
        // innermost stride (which equals the block's innermost stride iff the
        // block is C-contiguous and the view preserves the last axis).
        block_shape[ndim - 1] = std::uint64_t(block_elems) / prod;
        if (view_strides_elem[ndim - 1] != 1 && ndim > 1)
        {
            // C-contiguous block always has innermost stride 1 element.
            throw py::index_error(
                "strides in chunk are inefficient, not implemented!");
        }
    }

    // Decompose the data-pointer delta into per-axis offsets against the
    // block's row-major strides.
    std::uint64_t const view_ptr = reinterpret_cast<std::uintptr_t>(info.ptr);
    std::uint64_t const origin_ptr = reinterpret_cast<std::uintptr_t>(origin);
    if (view_ptr < origin_ptr)
    {
        throw py::index_error(
            "strides in chunk are inefficient, not implemented!");
    }
    std::uint64_t delta = (view_ptr - origin_ptr) / std::uint64_t(itemsize);

    std::vector<std::uint64_t> block_strides(ndim, 1u);
    {
        std::uint64_t acc = 1u;
        for (py::ssize_t d = ndim - 1; d >= 0; --d)
        {
            block_strides[d] = acc;
            acc *= block_shape[d];
        }
    }

    Offset mem_offset(ndim, 0u);
    {
        std::uint64_t rem = delta;
        for (py::ssize_t d = 0; d < ndim; ++d)
        {
            std::uint64_t const stride = block_strides[d];
            mem_offset[d] = stride == 0 ? 0 : rem / stride;
            rem = stride == 0 ? 0 : rem % stride;
        }
        if (rem != 0)
        {
            throw py::index_error(
                "strides in chunk are inefficient, not implemented!");
        }
    }

    // Validate that the view fits within the reconstructed block at the
    // computed offsets and that its strides match the block layout (this
    // rejects interleaved views such as `[:, ::2]` or `data[:, 5]`, whose
    // reconstruction is not a valid sub-cuboid).
    for (py::ssize_t d = 0; d < ndim; ++d)
    {
        if (mem_offset[d] + std::uint64_t(info.shape[d]) > block_shape[d])
        {
            throw py::index_error(
                "strides in chunk are inefficient, not implemented!");
        }
        // view strides must equal the block's contiguous strides
        if (std::uint64_t(info.strides[d]) / std::uint64_t(itemsize) !=
            block_strides[d])
        {
            throw py::index_error(
                "strides in chunk are inefficient, not implemented!");
        }
    }

    // If the view covers the whole block at offset 0, no selection is needed.
    bool whole = true;
    for (py::ssize_t d = 0; d < ndim; ++d)
    {
        if (mem_offset[d] != 0 ||
            std::uint64_t(info.shape[d]) != block_shape[d])
        {
            whole = false;
            break;
        }
    }
    if (whole)
    {
        return std::nullopt;
    }

    return MemorySelection{
        std::move(mem_offset), Extent(std::move(block_shape))};
}

/** Walk to the deepest owner of a buffer's memory block.
 *
 * Used by the memory-selection store/load paths: the backend expects the data
 * pointer to refer to the *origin* of the contiguous memory block, with the
 * selection given as {offset, extent}.
 *
 * Both numpy arrays (via their `.base` attribute) and generic buffer objects
 * such as memoryviews (via their `.obj` attribute) can be nested views; walk
 * the chain to the root object that owns the memory.
 *
 * @return The root owner as a Python object (an ndarray for numpy memory, or
 *         the object exposed through a generic buffer).
 */
inline py::object deepest_owner(py::object const &obj)
{
    py::object current = py::reinterpret_borrow<py::object>(obj);
    while (true)
    {
        py::object next;
        if (py::isinstance<py::array>(current))
        {
            // numpy array: follow `.base` (NumPy collapses nested views to a
            // flat owner array)
            next = current.attr("base");
        }
        else
        {
            // generic buffer (e.g. memoryview): follow `.obj`
            try
            {
                next = current.attr("obj");
            }
            catch (py::error_already_set const &)
            {
                break;
            }
        }
        if (next.is_none())
        {
            break;
        }
        current = next;
    }
    return current;
}

/*
 * Shared helpers for addressing (possibly strided) Python buffers as targets
 * of load/store operations.
 *
 * These are used by the lazy Load_Store_Chunk's `store()` / `into()` and by
 * the pass-through `load_chunk()` so that the buffer validation and the
 * memory-selection / deepest-owner / data-pointer bookkeeping live in exactly
 * one place.
 */

/** Validate that a buffer's shape exactly matches an expected shape.
 *
 * Used for the lazy chunk's `store()` and `into()` (and cross-checking in
 * `load_chunk`). Throws a `py::index_error` on mismatch.
 */
inline void check_buffer_shape(
    py::buffer_info const &info, std::vector<py::ssize_t> const &shape)
{
    if (size_t(info.ndim) != shape.size())
    {
        throw py::index_error(
            std::string("dimension of chunk (") + std::to_string(info.ndim) +
            std::string(
                "D) does not fit dimension of selection in record component "
                "(") +
            std::to_string(shape.size()) + std::string("D)"));
    }
    for (py::ssize_t d = 0; d < info.ndim; ++d)
    {
        if (info.shape[d] != shape[d])
        {
            throw py::index_error(
                std::string("size of chunk (") + std::to_string(info.shape[d]) +
                std::string(") for axis ") + std::to_string(d) +
                std::string(" does not match selection size ") +
                std::to_string(shape[d]));
        }
    }
}

/** The result of preparing a target buffer for a load/store operation. */
struct PreparedBufferTarget
{
    /** The object that owns the memory the backend will read/write (the
     *  deepest owner for memory selections, the buffer itself otherwise).
     *  Also used as the keep-alive handle captured by the operation. */
    py::object owner_obj;
    /** The address the backend reads/writes: the origin of the contiguous
     *  memory block for memory selections, the buffer's own pointer otherwise.
     */
    void *data_ptr = nullptr;
};

/** Derive a memory selection and resolve the address/handle for a buffer.
 *
 * @param buffer_obj The Python buffer object.
 * @param info Its `py::buffer_info`.
 * @param memsel The already-derived memory selection (from
 *               `derive_memory_selection`), or std::nullopt if the buffer
 *               covers the whole block.
 */
inline PreparedBufferTarget resolve_buffer_target(
    py::object buffer_obj,
    py::buffer_info const &info,
    std::optional<MemorySelection> const &memsel)
{
    py::object owner_obj = memsel.has_value()
        ? deepest_owner(buffer_obj)
        : py::reinterpret_borrow<py::object>(buffer_obj);
    void *data_ptr = info.ptr;
    if (memsel.has_value())
    {
        py::buffer owner_buf = py::cast<py::buffer>(owner_obj);
        data_ptr = owner_buf.request(/* writable = */ true).ptr;
    }
    return PreparedBufferTarget{std::move(owner_obj), data_ptr};
}

/** Throw the user-facing error for using a lazy chunk after the Series has
 *  been closed. */
[[noreturn]] inline void throw_closed_series_error(std::string const &detail)
{
    throw error::WrongAPIUsage(
        std::string(
            "Cannot load a lazily-created chunk: the underlying Series has "
            "already been closed. Load the chunk (e.g. via "
            "`np.asarray(rc[...])`) "
            "before closing the Series. ") +
        detail);
}

/* ==== relocated lazy load/store chunk support ==== */
namespace
{
/*
 * Small Python-exposed owner object that keeps a raw buffer alive.
 *
 * Used as the `base` of numpy arrays created from lazily loaded chunks: the
 * array holds a reference to this object instead of holding a reference to the
 * lazy chunk (which would create a reference cycle). Destroying the last
 * reference to the array releases this object and thereby the loaded buffer.
 */
struct PythonLoadBufferOwner
{
    explicit PythonLoadBufferOwner(std::shared_ptr<void> buffer)
        : data(std::move(buffer))
    {}

    std::shared_ptr<void> data;
};
} // namespace

PythonLazyLoadStoreChunk::PythonLazyLoadStoreChunk(
    ConfigureLoadStore operationBuilder, std::vector<py::ssize_t> shape)
    : m_operationBuilder(std::move(operationBuilder)), m_shape(std::move(shape))
{}

PythonLazyLoadStoreChunk::PythonLazyLoadStoreChunk(
    RecordComponent &rc,
    Offset offset,
    Extent extent,
    std::vector<py::ssize_t> shape)
    : PythonLazyLoadStoreChunk(
          rc.prepareLoadStore()
              .offset(std::move(offset))
              .extent(std::move(extent)),
          std::move(shape))
{}

auto const &PythonLazyLoadStoreChunk::shape() const
{
    return m_shape;
}

auto const &PythonLazyLoadStoreChunk::operationBuilder() const
{
    return m_operationBuilder;
}

auto &PythonLazyLoadStoreChunk::operationBuilder()
{
    return m_operationBuilder;
}

auto PythonLazyLoadStoreChunk::getDatatype() const -> Datatype
{
    return m_operationBuilder.getComponentHandle().getDatatype();
}

auto PythonLazyLoadStoreChunk::load() -> py::array &
{
    if (m_cache)
    {
        return *m_cache;
    }
    m_cache = doLoad();
    return *m_cache;
}

py::buffer_info PythonLazyLoadStoreChunk::getBuffer()
{
    py::array &arr = load();
    return arr.request();
}

void PythonLazyLoadStoreChunk::store(py::buffer const &buffer)
{
    auto info = buffer.request(/* writable = */ true);
    py::object buffer_obj = py::reinterpret_borrow<py::object>(buffer);

    // shape check: the buffer's shape must match the selection's shape
    check_buffer_shape(info, m_shape);

    auto memsel = derive_memory_selection(buffer_obj, info);

    // datatype check
    Datatype const buffer_dtype = dtype_from_bufferformat(info.format);
    if (buffer_dtype != getDatatype())
    {
        std::stringstream err;
        err << "Attempting store from Python buffer of type '" << buffer_dtype
            << "' into Record Component of type '" << getDatatype() << "'.";
        throw error::WrongAPIUsage(err.str());
    }

    auto target = resolve_buffer_target(buffer_obj, info, memsel);
    switchDatasetType<StoreChunkFromPythonArray>(
        getDatatype(),
        operationBuilder(),
        target.owner_obj,
        target.data_ptr,
        std::move(memsel));
}

py::object PythonLazyLoadStoreChunk::into(py::object const &buffer_obj)
{
    py::buffer buffer = py::cast<py::buffer>(buffer_obj);
    auto info = buffer.request(/* writable = */ true);

    // shape check: the buffer's shape must match the selection's shape
    check_buffer_shape(info, m_shape);

    auto memsel = derive_memory_selection(buffer_obj, info);

    // datatype check
    Datatype const buffer_dtype = dtype_from_bufferformat(info.format);
    if (buffer_dtype != getDatatype())
    {
        std::stringstream err;
        err << "Attempting load into Python buffer of type '" << buffer_dtype
            << "' from Record Component of type '" << getDatatype() << "'.";
        throw error::WrongAPIUsage(err.str());
    }

    auto target = resolve_buffer_target(buffer_obj, info, memsel);

    try
    {
        switchDatasetType<LoadChunkIntoBufferSynchronously>(
            getDatatype(),
            operationBuilder(),
            target.owner_obj,
            target.data_ptr,
            std::move(memsel));
    }
    catch (error::Internal const &e)
    {
        throw_closed_series_error(e.what());
    }
    catch (std::runtime_error const &e)
    {
        // In builds with openPMD_USE_INVASIVE_TESTS, the closed-iteration
        // guard in RecordComponentData::push_chunk fires synchronously
        // (before the deferred-flush machinery can produce the clean
        // error::Internal above). Translate that into the same
        // user-friendly Wrong API usage error.
        std::string const msg = e.what();
        if (msg.find("closed") != std::string::npos)
        {
            throw_closed_series_error(msg);
        }
        throw;
    }
    return py::reinterpret_borrow<py::object>(buffer_obj);
}

auto PythonLazyLoadStoreChunk::doLoad() -> py::array
{
    auto dtype = getDatatype();
    // Use the chaining API's allocating load (with automatic flush upon
    // evaluation): it allocates the buffer, enqueues the read and fills
    // the buffer when the returned DeferredComputation is evaluated.
    //
    // TODO: The garbage collector should keep the Series alive as long as
    // the handle lives.
    //
    // Loading a chunk requires the underlying Series to still be open. If
    // the Series was closed before the lazy chunk is resolved, the
    // ConfigureLoadStore machinery throws (cf. the IOHandler guard in
    // ConfigureLoadStore::deferFlush); catch that and rethrow it with a
    // user-friendly message instead of crashing or handing out
    // uninitialized memory.
    std::shared_ptr<void> buffer;
    try
    {
        auto loadVar = operationBuilder().loadVariant();
        auto shared = loadVar(); // evaluates: flushes (executes the read)
        buffer = std::visit(
            [](auto &ptr) -> std::shared_ptr<void> {
                return std::static_pointer_cast<void>(std::move(ptr));
            },
            shared);
    }
    catch (error::Internal const &e)
    {
        throw_closed_series_error(e.what());
    }
    m_data = std::move(buffer);

    // Wrap the loaded buffer in a numpy array that (transitively) owns the
    // memory through a PythonLoadBufferOwner. The returned array keeps the
    // owner -- and hence the buffer -- alive; no reference cycle is
    // created with this lazy chunk.
    //
    // TODO check if this duplicates bits from the old allocating Python API
    py::object owner =
        py::cast(std::make_shared<PythonLoadBufferOwner>(m_data));
    py::array arr(
        dtype_to_numpy(dtype),
        py::array::ShapeContainer(m_shape),
        py::array::ShapeContainer(strides_from_extent()),
        m_data.get(),
        owner);
    return arr;
}

auto PythonLazyLoadStoreChunk::strides_from_extent() -> std::vector<py::ssize_t>
{
    // C-contiguous row-major strides
    std::vector<py::ssize_t> strides(m_shape.size());
    py::ssize_t acc =
        static_cast<py::ssize_t>(openPMD::detail::dtypeSize(getDatatype()));
    for (size_t d = m_shape.size(); d > 0; --d)
    {
        strides[d - 1] = acc;
        acc *= m_shape[d - 1];
    }
    return strides;
}

namespace
{
/** Convert a py::tuple / slice / int index into a PythonLazyLoadStoreChunk.
 *
 * Returns the lazy handle without performing I/O. The shape already has
 * integer-indexed (flattened) axes removed, matching what a numpy array over
 * the selection would look like.
 */
inline PythonLazyLoadStoreChunk
make_lazy_chunk(RecordComponent &rc, py::tuple const &slices)
{
    uint8_t ndim = rc.getDimensionality();
    auto const full_extent = rc.getExtent();

    Offset offset;
    Extent extent;
    std::vector<bool> flatten;
    std::tie(offset, extent, flatten) =
        parseTupleSlices(ndim, full_extent, slices);

    // shape with flattened axes removed
    std::vector<py::ssize_t> shape;
    for (size_t d = 0; d < extent.size(); ++d)
    {
        if (!flatten[d])
        {
            shape.push_back(static_cast<py::ssize_t>(extent[d]));
        }
    }

    return PythonLazyLoadStoreChunk(
        rc, std::move(offset), std::move(extent), std::move(shape));
}
} // namespace

struct PythonDynamicMemoryView
{
    using ShapeContainer = pybind11::array::ShapeContainer;

    template <typename T>
    PythonDynamicMemoryView(
        DynamicMemoryView<T> dynamicView,
        ShapeContainer arrayShape,
        ShapeContainer strides)
        : m_dynamicView(
              std::shared_ptr<void>(
                  new DynamicMemoryView<T>(std::move(dynamicView))))
        , m_arrayShape(std::move(arrayShape))
        , m_strides(std::move(strides))
        , m_datatype(determineDatatype<T>())
    {}

    [[nodiscard]] pybind11::object currentView() const;

    std::shared_ptr<void> m_dynamicView;
    ShapeContainer m_arrayShape;
    ShapeContainer m_strides;
    Datatype m_datatype;
};

namespace
{
struct GetCurrentView
{
    template <typename T>
    static pybind11::object call(PythonDynamicMemoryView const &dynamicView)
    {
        auto span =
            static_cast<DynamicMemoryView<T> *>(dynamicView.m_dynamicView.get())
                ->currentBuffer();
        if (!span.data())
        {
            /*
             * Fallback for zero-sized store_chunk calls.
             * py::memoryview cannot be used since it checks for nullpointers,
             * even when the extent is zero.
             * This may sound like an esoteric use case at first, but may happen
             * in parallel usage when the chunk distribution ends up assigning
             * zero-sized chunks to some rank.
             */
            return py::array(
                dtype_to_numpy(dynamicView.m_datatype),
                dynamicView.m_arrayShape,
                dynamicView.m_strides);
        }
        else
        {
            return py::memoryview::from_buffer(
                span.data(),
                dynamicView.m_arrayShape,
                dynamicView.m_strides,
                /* readonly = */ false);
        }
    }

    static constexpr char const *errorMsg = "DynamicMemoryView";
};
} // namespace

pybind11::object PythonDynamicMemoryView::currentView() const
{
    return switchDatasetType<GetCurrentView>(m_datatype, *this);
}

namespace
{
struct StoreChunkSpan
{
    template <typename T>
    static PythonDynamicMemoryView
    call(RecordComponent &r, Offset const &offset, Extent const &extent)
    {
        DynamicMemoryView<T> dynamicView = r.storeChunk<T>(offset, extent);
        pybind11::array::ShapeContainer arrayShape(
            extent.begin(), extent.end());
        std::vector<py::ssize_t> strides(extent.size());
        {
            py::ssize_t accumulator = sizeof(T);
            size_t dim = extent.size();
            while (dim > 0)
            {
                --dim;
                strides[dim] = accumulator;
                accumulator *= extent[dim];
            }
        }
        return PythonDynamicMemoryView(
            std::move(dynamicView),
            std::move(arrayShape),
            py::array::ShapeContainer(std::move(strides)));
    }

    static constexpr char const *errorMsg = "RecordComponent.store_chunk()";
};
} // namespace

inline PythonDynamicMemoryView store_chunk_span(
    RecordComponent &r,
    Offset const &offset,
    Extent const &extent,
    std::vector<bool> const &flatten)
{
    // some one-size dimensions might be flattended in our output due to
    // selections by index
    size_t const numFlattenDims =
        std::count(flatten.begin(), flatten.end(), true);
    std::vector<ptrdiff_t> shape(extent.size() - numFlattenDims);
    auto maskIt = flatten.begin();
    std::copy_if(
        std::begin(extent),
        std::end(extent),
        std::begin(shape),
        [&maskIt](std::uint64_t) { return !*(maskIt++); });

    return switchDatasetType<StoreChunkSpan>(
        r.getDatatype(), r, offset, extent);
}

inline PythonDynamicMemoryView
store_chunk_span(RecordComponent &r, py::tuple const &slices)
{
    uint8_t ndim = r.getDimensionality();
    auto const full_extent = r.getExtent();

    Offset offset;
    Extent extent;
    std::vector<bool> flatten;
    std::tie(offset, extent, flatten) =
        parseTupleSlices(ndim, full_extent, slices);

    return store_chunk_span(r, offset, extent, flatten);
}

/** Load Chunk (generic buffer)
 *
 * Called with offset and extent that are already in the record component's
 * dimension.
 *
 * Size checks of the requested chunk (spanned data is in valid bounds)
 * will be performed at C++ API part in RecordComponent::loadChunk .
 *
 * This overload works on any PEP 3118 buffer (`py::buffer`), not only numpy
 * arrays: numpy arrays, memoryviews, `array.array`, raw bytes, and custom
 * buffer exporters are all accepted. The buffer protocol is unpacked directly
 * via `py::buffer_info`, so no round-trip through numpy (`py::array::ensure`)
 * is required.
 *
 * If the destination buffer is a (possibly non-contiguous in its own shape)
 * sub-cuboid view of a larger contiguous buffer, a memory selection is derived
 * and the dataset chunk is loaded directly into that sub-region through a
 * single `prepareLoadStore().memorySelection().load()` operation, avoiding an
 * intermediate buffer. e.g. loading into a strided view of a larger
 * ghost-cell-style buffer:
 *
 *   record_component.load_chunk(
 *       offset, extent, read_buffer[2:4, 2:4, 2:4])
 *
 * If the destination buffer is contiguous (or owns its memory), the ordinary
 * contiguous load path is used.
 */
inline void load_chunk(
    RecordComponent &r,
    py::buffer &buffer,
    Offset const &offset,
    Extent const &extent)
{
    auto info = buffer.request(/* writable = */ true);
    py::object buffer_obj = py::reinterpret_borrow<py::object>(buffer);

    // check buffer is large enough
    size_t s_load = 1u;
    size_t s_array = 1u;
    std::string str_extent_shape;
    std::string str_buffer_shape;
    for (auto &si : extent)
    {
        s_load *= si;
        str_extent_shape.append(" ").append(std::to_string(si));
    }
    for (py::ssize_t d = 0; d < info.ndim; ++d)
    {
        s_array *= info.shape[d];
        str_buffer_shape.append(" ").append(std::to_string(info.shape[d]));
    }

    if (s_array < s_load)
    {
        throw py::index_error(
            std::string("size of buffer (") + std::to_string(s_array) +
            std::string("; shape:") + str_buffer_shape +
            std::string(
                ") is smaller than size of selection "
                "in record component (") +
            std::to_string(s_load) + std::string("; shape:") +
            str_extent_shape + std::string(")"));
    }

    auto memsel = derive_memory_selection(buffer_obj, info);
    if (!memsel.has_value())
    {
        check_buffer_is_contiguous(info);
    }

    // datatype check: the buffer's PEP 3118 format string must map to the
    // record component's datatype
    Datatype const buffer_dtype = dtype_from_bufferformat(info.format);
    if (buffer_dtype != r.getDatatype())
    {
        std::stringstream err;
        err << "Attempting load into Python buffer of type '" << buffer_dtype
            << "' from Record Component of type '" << r.getDatatype() << "'.";
        throw error::WrongAPIUsage(err.str());
    }

    auto target = resolve_buffer_target(buffer_obj, info, memsel);
    switchDatasetType<LoadChunkIntoPythonArray>(
        r.getDatatype(),
        r,
        target.owner_obj,
        target.data_ptr,
        offset,
        extent,
        std::move(memsel));
}

/** Load Chunk (numpy array convenience overload)
 *
 * See the generic overload above; this just forwards a numpy array through the
 * generic buffer path.
 */
inline void load_chunk(
    RecordComponent &r,
    py::array &a,
    Offset const &offset,
    Extent const &extent)
{
    load_chunk(r, static_cast<py::buffer &>(a), offset, extent);
}

auto load_chunk_lazy(RecordComponent &rc, py::tuple const &slices)
    -> PythonLazyLoadStoreChunk
{
    return make_lazy_chunk(rc, slices);
}

auto load_chunk_lazy_slice(RecordComponent &rc, py::slice const &slice_obj)
    -> PythonLazyLoadStoreChunk
{
    auto const slices = py::make_tuple(slice_obj);
    return load_chunk_lazy(rc, slices);
}

auto load_chunk_lazy_int(RecordComponent &rc, py::int_ const &slice_obj)
    -> PythonLazyLoadStoreChunk
{
    auto const slices = py::make_tuple(slice_obj);
    return load_chunk_lazy(rc, slices);
}

void store_chunk_object(
    RecordComponent &r, py::tuple const &slices, py::object value)
{
    // If the value is another lazy chunk handle, resolve it first (this
    // performs the load through openPMD) and store the resulting array.
    if (py::isinstance<PythonLazyLoadStoreChunk>(value))
    {
        auto &other = value.cast<PythonLazyLoadStoreChunk &>();
        py::array arr = other.load();
        store_chunk(r, arr, slices);
        return;
    }
    // Otherwise, accept any buffer-ish object (numpy array, memoryview,
    // etc.) by converting it to a numpy contiguous array.
    py::array arr;
    try
    {
        arr = py::array::ensure(value);
    }
    catch (py::error_already_set const &)
    {
        throw error::WrongAPIUsage(
            "Cannot assign: RHS is neither a lazy chunk handle nor a "
            "buffer (numpy array / memoryview / ...).");
    }
    if (!arr)
    {
        throw error::WrongAPIUsage(
            "Cannot assign: RHS is neither a lazy chunk handle nor a "
            "buffer (numpy array / memoryview / ...).");
    }
    store_chunk(r, arr, slices);
}

void store_chunk_object_slice(
    RecordComponent &r, py::slice const &slice_obj, py::object value)
{
    auto const slices = py::make_tuple(slice_obj);
    store_chunk_object(r, slices, std::move(value));
}

void store_chunk_object_int(
    RecordComponent &r, py::int_ const &slice_obj, py::object value)
{
    auto const slices = py::make_tuple(slice_obj);
    store_chunk_object(r, slices, std::move(value));
}

void init_RecordComponent(py::module &m)
{
    py::class_<PythonLoadBufferOwner, std::shared_ptr<PythonLoadBufferOwner>>(
        m, "_Load_Buffer_Owner");

    py::class_<PythonLazyLoadStoreChunk>(
        m, "Load_Store_Chunk", py::buffer_protocol())
        .def_buffer(&PythonLazyLoadStoreChunk::getBuffer)
        .def_property_readonly(
            "shape",
            [](PythonLazyLoadStoreChunk const &self) {
                py::list shape;
                for (auto s : self.shape())
                {
                    shape.append(s);
                }
                return py::tuple(shape);
            })
        .def_property_readonly(
            "ndim",
            [](PythonLazyLoadStoreChunk const &self) {
                return self.shape().size();
            })
        .def_property_readonly(
            "dtype",
            [](PythonLazyLoadStoreChunk const &self) {
                return dtype_to_numpy(self.getDatatype());
            })
        // .def_property_readonly(
        //     "offset",
        //     [](PythonLazyLoadStoreChunk const &self) { return self.offset();
        //     })
        // .def_property_readonly(
        //     "extent",
        //     [](PythonLazyLoadStoreChunk const &self) { return self.extent();
        //     })
        .def(
            "__array__",
            [](PythonLazyLoadStoreChunk &self,
               py::object dtype,
               const py::object &copy) -> py::object {
                // numpy calls __array__(dtype=..., copy=...) whenever it
                // consumes the object (np.asarray, assignment RHS, ...).
                // This is the point where the lazy load is actually performed.
                py::array arr = self.load();
                bool const want_copy = !copy.is_none() && py::cast<bool>(copy);
                if (!dtype.is_none())
                {
                    // Let numpy handle dtype conversion and copy semantics.
                    py::object np = py::module::import("numpy");
                    return np.attr("array")(
                        arr,
                        py::arg("dtype") = dtype,
                        py::arg("copy") = want_copy);
                }
                if (want_copy)
                {
                    return arr.attr("copy")();
                }
                return py::reinterpret_borrow<py::object>(arr);
            },
            py::arg("dtype") = py::none(),
            py::arg("copy") = py::none())
        .def(
            "__reduce__",
            [](PythonLazyLoadStoreChunk &self) -> py::tuple {
                // Make the lazy chunk picklable: pickling materializes the
                // data (performing the load, which keeps the Series open in
                // the common multiprocessing scenario) and returns it as a
                // plain numpy array. `np.array(arr)` copies by default, so
                // the unpickled object is an owning ndarray.
                py::array arr = self.load();
                return py::make_tuple(
                    py::module::import("numpy").attr("array"),
                    py::make_tuple(arr));
            })
        .def(
            "__getitem__",
            [](PythonLazyLoadStoreChunk &self, py::object idx) -> py::object {
                py::array arr = self.load();
                return py::reinterpret_borrow<py::object>(
                    arr.attr("__getitem__")(idx));
            })
        .def(
            "load",
            [](PythonLazyLoadStoreChunk &self) -> py::array {
                return self.load();
            })
        .def(
            "store",
            [](PythonLazyLoadStoreChunk &self, py::buffer const &buffer) {
                self.store(buffer);
            })
        .def(
            "into",
            [](PythonLazyLoadStoreChunk &self, py::object buffer_obj) {
                return self.into(std::move(buffer_obj));
            },
            py::arg("target buffer"))
        .def(
            "__repr__",
            [](PythonLazyLoadStoreChunk const &self) {
                std::stringstream stream;
                stream << "<openPMD.Load_Store_Chunk of type '"
                       << self.getDatatype() << "' and with shape (";
                auto const &shape = self.shape();
                for (size_t i = 0; i < shape.size(); ++i)
                {
                    if (i)
                    {
                        stream << ", ";
                    }
                    stream << shape[i];
                }
                stream << ")> (lazy; not yet loaded)";
                return stream.str();
            })
        .def(
            "__iter__",
            [](PythonLazyLoadStoreChunk &self) {
                py::array arr = self.load();
                return py::iter(arr);
            })
        .def("__len__", [](PythonLazyLoadStoreChunk const &self) {
            if (self.shape().empty())
            {
                return (py::ssize_t)1;
            }
            return self.shape()[0];
        });

    py::class_<PythonDynamicMemoryView>(m, "Dynamic_Memory_View")
        .def(
            "__repr__",
            [](PythonDynamicMemoryView const &view) {
                return "<openPMD.Dynamic_Memory_view of dimensionality '" +
                    std::to_string(view.m_arrayShape->size()) + "'>";
            })
        .def("current_buffer", [](PythonDynamicMemoryView const &view) {
            return view.currentView();
        });

    auto py_rc_cnt =
        declare_container<PyRecordComponentContainer, Attributable>(
            m, "Record_Component_Container");

    py::class_<RecordComponent, BaseRecordComponent> cl(m, "Record_Component");
    cl.def(
          "__repr__",
          [](RecordComponent const &rc) {
              std::stringstream stream;
              stream << "<openPMD.Record_Component of type '"
                     << rc.getDatatype() << "' and with extent ";
              if (auto extent = rc.getExtent(); extent.empty())
              {
                  stream << "[]>";
              }
              else
              {
                  auto begin = extent.begin();
                  stream << '[' << *begin++;
                  for (; begin != extent.end(); ++begin)
                  {
                      stream << ", " << *begin;
                  }
                  stream << "]>";
              }
              return stream.str();
          })

        .def_property(
            "unit_SI",
            &BaseRecordComponent::unitSI,
            &RecordComponent::setUnitSI)

        .def("reset_dataset", &RecordComponent::resetDataset)

        .def_property_readonly("ndim", &RecordComponent::getDimensionality)
        .def_property_readonly("shape", &RecordComponent::getExtent)
        .def_property_readonly("empty", &RecordComponent::empty)

        // buffer types
        .def(
            "make_constant",
            [](RecordComponent &rc, py::buffer &a) {
                py::buffer_info buf = a.request();
                auto const dtype = dtype_from_bufferformat(buf.format);

                using DT = Datatype;

                // allow one-element n-dimensional buffers as well
                py::ssize_t numElements = 1;
                if (buf.ndim > 0)
                {
                    for (auto d = 0; d < buf.ndim; ++d)
                        numElements *= buf.shape.at(d);
                }

                // Numpy: Handling of arrays and scalars
                // work-around for
                // https://github.com/pybind/pybind11/issues/1224
                // -> passing numpy scalars as buffers needs numpy 1.15+
                //    https://github.com/numpy/numpy/issues/10265
                //    https://github.com/pybind/pybind11/issues/1224#issuecomment-354357392
                // scalars, see PEP 3118
                // requires Numpy 1.15+
                if (numElements == 1)
                {
                    // refs:
                    //   https://docs.scipy.org/doc/numpy-1.15.0/reference/arrays.interface.html
                    //   https://docs.python.org/3/library/struct.html#format-characters
                    // std::cout << "  scalar type '" << buf.format << "'" <<
                    // std::endl; typestring: encoding + type + number of bytes
                    switch (dtype)
                    {
                    case DT::CHAR:
                        return rc.makeConstant(*static_cast<char *>(buf.ptr));
                        break;
                    case DT::SHORT:
                        return rc.makeConstant(*static_cast<short *>(buf.ptr));
                        break;
                    case DT::INT:
                        return rc.makeConstant(*static_cast<int *>(buf.ptr));
                        break;
                    case DT::LONG:
                        return rc.makeConstant(*static_cast<long *>(buf.ptr));
                        break;
                    case DT::LONGLONG:
                        return rc.makeConstant(
                            *static_cast<long long *>(buf.ptr));
                        break;
                    case DT::UCHAR:
                        return rc.makeConstant(
                            *static_cast<unsigned char *>(buf.ptr));
                        break;
                    case DT::USHORT:
                        return rc.makeConstant(
                            *static_cast<unsigned short *>(buf.ptr));
                        break;
                    case DT::UINT:
                        return rc.makeConstant(
                            *static_cast<unsigned int *>(buf.ptr));
                        break;
                    case DT::ULONG:
                        return rc.makeConstant(
                            *static_cast<unsigned long *>(buf.ptr));
                        break;
                    case DT::ULONGLONG:
                        return rc.makeConstant(
                            *static_cast<unsigned long long *>(buf.ptr));
                        break;
                    case DT::FLOAT:
                        return rc.makeConstant(*static_cast<float *>(buf.ptr));
                        break;
                    case DT::DOUBLE:
                        return rc.makeConstant(*static_cast<double *>(buf.ptr));
                        break;
                    case DT::LONG_DOUBLE:
                        return rc.makeConstant(
                            *static_cast<long double *>(buf.ptr));
                        break;
                    case DT::CFLOAT:
                        return rc.makeConstant(
                            *static_cast<std::complex<float> *>(buf.ptr));
                        break;
                    case DT::CDOUBLE:
                        return rc.makeConstant(
                            *static_cast<std::complex<double> *>(buf.ptr));
                        break;
                    case DT::CLONG_DOUBLE:
                        return rc.makeConstant(
                            *static_cast<std::complex<long double> *>(buf.ptr));
                        break;
                    case DT::BOOL:
                        throw std::runtime_error(
                            "make_constant: "
                            "Boolean type not supported!");
                        break;
                    default:
                        throw std::runtime_error(
                            "make_constant: "
                            "Unknown Datatype!");
                    }
                }
                else
                {
                    throw std::runtime_error(
                        "make_constant: "
                        "Only scalar values supported!");
                }
            },
            py::arg("value"))
        // allowed python intrinsics, after (!) buffer matching
        .def(
            "make_constant",
            &RecordComponent::makeConstant<char>,
            py::arg("value"))
        .def(
            "make_constant",
            &RecordComponent::makeConstant<long>,
            py::arg("value"))
        .def(
            "make_constant",
            &RecordComponent::makeConstant<double>,
            py::arg("value"))
        .def(
            "make_empty",
            [](RecordComponent &rc, Datatype dt, uint8_t dimensionality) {
                return rc.makeEmpty(dt, dimensionality);
            },
            py::arg("datatype"),
            py::arg("dimensionality"))
        .def(
            "make_empty",
            [](RecordComponent &rc,
               pybind11::object dt,
               uint8_t dimensionality) {
                return rc.makeEmpty(
                    dtype_from_numpy(std::move(dt)), dimensionality);
            })

        // deprecated: pass-through C++ API
        .def(
            "load_chunk",
            [](RecordComponent &r,
               Offset const &offset_in,
               Extent const &extent_in) {
                uint8_t ndim = r.getDimensionality();

                // default arguments
                //   offset = {0u}: expand to right dim {0u, 0u, ...}
                Offset offset = offset_in;
                if (offset_in.size() == 1u && offset_in.at(0) == 0u)
                    offset = Offset(ndim, 0u);

                //   extent = {-1u}: take full size
                Extent extent(ndim, 1u);
                if (extent_in.size() == 1u && extent_in.at(0) == -1u)
                {
                    extent = r.getExtent();
                    for (uint8_t i = 0u; i < ndim; ++i)
                        extent[i] -= offset[i];
                }
                else
                    extent = extent_in;

                std::vector<ptrdiff_t> shape(extent.size());
                std::copy(
                    std::begin(extent), std::end(extent), std::begin(shape));
                auto const dtype = dtype_to_numpy(r.getDatatype());
                auto a = py::array(dtype, shape);
                load_chunk(r, a, offset, extent);

                return a;
            },
            py::arg_v(
                "offset", Offset(1, 0u), "np.zeros(Record_Component.shape)"),
            py::arg_v("extent", Extent(1, -1u), "Record_Component.shape"))
        .def(
            "load_chunk",
            [](RecordComponent &r,
               py::buffer buffer,
               Offset const &offset_in,
               Extent const &extent_in) {
                uint8_t ndim = r.getDimensionality();

                // default arguments
                //   offset = {0u}: expand to right dim {0u, 0u, ...}
                Offset offset = offset_in;
                if (offset_in.size() == 1u && offset_in.at(0) == 0u)
                    offset = Offset(ndim, 0u);

                //   extent = {-1u}: take full size
                Extent extent(ndim, 1u);
                if (extent_in.size() == 1u && extent_in.at(0) == -1u)
                {
                    extent = r.getExtent();
                    for (uint8_t i = 0u; i < ndim; ++i)
                        extent[i] -= offset[i];
                }
                else
                    extent = extent_in;

                std::vector<bool> flatten(ndim, false);
                load_chunk(r, buffer, offset, extent);
            },
            py::arg("pre-allocated buffer"),
            py::arg_v(
                "offset", Offset(1, 0u), "np.zeros(Record_Component.shape)"),
            py::arg_v("extent", Extent(1, -1u), "Record_Component.shape"))

        // deprecated: pass-through C++ API
        .def(
            "store_chunk",
            [](RecordComponent &r,
               py::array &a,
               Offset const &offset_in,
               Extent const &extent_in) {
                // default arguments
                //   offset = {0u}: expand to right dim {0u, 0u, ...}
                Offset offset = offset_in;
                if (offset_in.size() == 1u && offset_in.at(0) == 0u &&
                    a.ndim() > 1)
                    offset = Offset(a.ndim(), 0u);

                //   extent = {-1u}: take full size
                Extent extent(a.ndim(), 1u);
                if (extent_in.size() == 1u && extent_in.at(0) == -1u)
                    for (auto d = 0; d < a.ndim(); ++d)
                        extent.at(d) = a.shape()[d];
                else
                    extent = extent_in;

                std::vector<bool> flatten(r.getDimensionality(), false);
                store_chunk(r, a, offset, extent, flatten);
            },
            py::arg("array"),
            py::arg_v("offset", Offset(1, 0u), "np.zeros_like(array)"),
            py::arg_v("extent", Extent(1, -1u), "array.shape"))
        .def(
            "store_chunk",
            [](RecordComponent &r,
               Offset const &offset_in,
               Extent const &extent_in) {
                // default arguments
                //   offset = {0u}: expand to right dim {0u, 0u, ...}
                unsigned dimensionality = r.getDimensionality();
                Extent const &totalExtent = r.getExtent();
                Offset offset = offset_in;
                if (offset_in.size() == 1u && offset_in.at(0) == 0u &&
                    dimensionality > 1u)
                    offset = Offset(dimensionality, 0u);

                //   extent = {-1u}: take full size
                Extent extent(dimensionality, 1u);
                if (extent_in.size() == 1u && extent_in.at(0) == -1u)
                    for (unsigned d = 0; d < dimensionality; ++d)
                        extent.at(d) = totalExtent[d];
                else
                    extent = extent_in;

                std::vector<bool> flatten(r.getDimensionality(), false);
                return store_chunk_span(r, offset, extent, flatten);
            },
            py::arg_v("offset", Offset(1, 0u), "np.zeros_like(array)"),
            py::arg_v("extent", Extent(1, -1u), "array.shape"))

        .def_property_readonly_static(
            "SCALAR",
            [](py::object const &) { return RecordComponent::SCALAR; })

        // TODO remove in future versions (deprecated)
        .def("set_unit_SI", &RecordComponent::setUnitSI) // deprecated
        ;
    add_pickle(
        cl,
        [](std::shared_ptr<openPMD::Series> series,
           std::vector<std::string> const &group) {
            uint64_t const n_it = std::stoull(group.at(1));
            auto res = series->iterations[n_it]
                           .open()
                           .particles[group.at(3)][group.at(4)]
                                     [group.size() < 6 ? RecordComponent::SCALAR
                                                       : group.at(5)];
            return internal::makeOwning(res, std::move(series));
        });

    addRecordComponentSetGet(cl);

    finalize_container<PyRecordComponentContainer>(py_rc_cnt);
    addRecordComponentSetGet(
        finalize_container<PyBaseRecordRecordComponent>(
            declare_container<
                PyBaseRecordRecordComponent,
                PyRecordComponentContainer,
                RecordComponent>(m, "Base_Record_Record_Component")))
        .def_property_readonly(
            "scalar",
            &BaseRecord<RecordComponent>::scalar,
            &docstring::is_scalar[1]);

    py::enum_<RecordComponent::Allocation>(m, "Allocation")
        .value("USER", RecordComponent::Allocation::USER)
        .value("API", RecordComponent::Allocation::API)
        .value("AUTO", RecordComponent::Allocation::AUTO);
}
