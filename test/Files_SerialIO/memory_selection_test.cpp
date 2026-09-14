/* Copyright 2026 openPMD contributors
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
#include "SerialIOTests.hpp"
#include "openPMD/openPMD.hpp"

#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <vector>

namespace memory_selection_test
{
using namespace openPMD;

/*
 * Test that a sub-region of a contiguous memory buffer can be stored into /
 * loaded from a dataset through a memory selection:
 *
 *   prepareLoadStore()
 *       .offset(...).extent(...)
 *       .withContiguousContainer(buffer)
 *       .memorySelection({{start}, {bufferShape}})
 *       .store();  // or .load();
 *
 * The data pointer refers to the origin of `buffer`; the memory selection
 * selects a chunk that is placed at `start` within `buffer`.
 */
void memory_selection_write_and_read(std::string const &file_ending)
{
    std::string const name = "../samples/memory_selection_" + file_ending;

    /*
     * A (5, 5) dataset. The memory buffer is a (5, 5) contiguous array with a
     * 3x3 interior; we store the interior into the dataset's interior.
     */
    std::vector<int> buffer(5 * 5);
    std::iota(buffer.begin(), buffer.end(), 0);
    // rows: 0 1 2 3 4
    //       5 6 7 8 9
    //      10 11 12 13 14
    //      15 16 17 18 19
    //      20 21 22 23 24
    {
        Series write(name, Access::CREATE);
        auto it = write.writeIterations()[0];
        auto E_x = it.meshes["E"]["x"];
        E_x.resetDataset(Dataset(Datatype::INT, {5, 5}));
        // store the 3x3 interior of the buffer into dataset[1:4, 1:4]
        E_x.prepareLoadStore()
            .offset({1, 1})
            .extent({3, 3})
            .withContiguousContainer(buffer)
            .memorySelection({{1, 1}, {5, 5}})
            .store();
        write.close();
    }

    {
        Series read(name, Access::READ_ONLY);
        auto it = read.iterations[0];
        auto E_x = it.meshes["E"]["x"];
        auto data = E_x.prepareLoadStore().extent({5, 5}).load<int>().get();
        read.flush();

        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                // dataset[1+row][1+col] == buffer[1+row][1+col]
                int const expected = buffer[(1 + row) * 5 + (1 + col)];
                REQUIRE(data.get()[(1 + row) * 5 + (1 + col)] == expected);
            }
        }
    }

    /*
     * Load a chunk into a sub-region of a larger buffer through a memory
     * selection on the read side: load dataset[1:4, 1:4] into the center of a
     * (5, 5) buffer (whose memory selection places it at (1, 1)).
     */
    std::vector<int> read_buffer(5 * 5, -1);
    {
        Series read(name, Access::READ_ONLY);
        auto it = read.iterations[0];
        auto E_x = it.meshes["E"]["x"];
        E_x.prepareLoadStore()
            .offset({1, 1})
            .extent({3, 3})
            .withContiguousContainer(read_buffer)
            .memorySelection({{1, 1}, {5, 5}})
            .unsafeNoAutomaticFlush()
            .load();
        read.flush();
    }
    // the interior was filled, the border remains untouched
    for (int i = 0; i < 5; ++i)
    {
        for (int j = 0; j < 5; ++j)
        {
            bool const inside = i >= 1 && i < 4 && j >= 1 && j < 4;
            if (inside)
            {
                REQUIRE(read_buffer[i * 5 + j] == buffer[i * 5 + j]);
            }
            else
            {
                REQUIRE(read_buffer[i * 5 + j] == -1);
            }
        }
    }
}

auto memory_selection_test() -> void
{
#if openPMD_HAVE_HDF5
    memory_selection_write_and_read(".h5");
#endif
}
} // namespace memory_selection_test
