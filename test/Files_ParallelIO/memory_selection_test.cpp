/* Copyright 2025 Franz Poeschel
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
#include "ParallelIOTests.hpp"

#include "openPMD/IO/ADIOS/macros.hpp"
#include "openPMD/auxiliary/Filesystem.hpp"
#include "openPMD/openPMD.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#if openPMD_HAVE_ADIOS2 && openPMD_HAVE_MPI
#include <mpi.h>

namespace memory_selection_test
{
void memory_selection_read_test(std::string const &file_ending)
{
    int r_mpi_rank{-1}, r_mpi_size{-1};
    MPI_Comm_rank(MPI_COMM_WORLD, &r_mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &r_mpi_size);
    unsigned mpi_rank{static_cast<unsigned>(r_mpi_rank)},
        mpi_size{static_cast<unsigned>(r_mpi_size)};
    std::string name = "../samples/memory_selection_read." + file_ending;

    // Each rank writes a row into a (mpi_size, 4) dataset.
    {
        Series write(name, Access::CREATE, MPI_COMM_WORLD);
        Iteration it0 = write.iterations[0];
        auto E_x = it0.meshes["E"]["x"];
        E_x.resetDataset({Datatype::INT, {mpi_size, 4}});
        std::vector<int> data{
            int(10u * mpi_rank + 0),
            int(10u * mpi_rank + 1),
            int(10u * mpi_rank + 2),
            int(10u * mpi_rank + 3)};
        E_x.storeChunk(data, {mpi_rank, 0}, {1, 4});
        it0.close();
    }

    {
        Series read(
            name,
            Access::READ_ONLY,
            MPI_COMM_WORLD,
            R"({"verify_homogeneous_extents": false})");
        Iteration it0 = read.iterations[0];
        auto E_x = it0.meshes["E"]["x"];

        // Load the full dataset into a buffer that is larger than the chunk in
        // each dimension, using a memory selection to scatter the loaded chunk
        // into the buffer (ghost-cell style).
        // Dataset: (mpi_size, 4); buffer: (mpi_size + 2, 6), selecting the
        // interior sub-block starting at (1, 1).
        size_t const buffer_rows = mpi_size + 2;
        size_t const buffer_cols = 6;
        std::vector<int> buffer(buffer_rows * buffer_cols, -1);

        E_x.prepareLoadStore()
            .withContiguousContainer(buffer)
            .memorySelection({{1, 1}, {buffer_rows, buffer_cols}})
            .load()
            .get();

        // Check the interior sub-block (rows 1..mpi_size, cols 1..4) contains
        // the dataset values.
        for (size_t row = 0; row < mpi_size; ++row)
        {
            for (size_t col = 0; col < 4; ++col)
            {
                int expected = int(10u * row + col);
                REQUIRE(
                    buffer[(row + 1) * buffer_cols + (col + 1)] == expected);
            }
        }
        // And the rest remains untouched (=-1).
        for (size_t i = 0; i < buffer.size(); ++i)
        {
            size_t row = i / buffer_cols;
            size_t col = i % buffer_cols;
            bool inside =
                row >= 1 && row < 1 + mpi_size && col >= 1 && col < 1 + 4;
            if (!inside)
            {
                REQUIRE(buffer[i] == -1);
            }
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (auxiliary::directory_exists(name))
    {
        auxiliary::remove_directory(name);
    }
    MPI_Barrier(MPI_COMM_WORLD);
}

auto memory_selection_test() -> void
{
    memory_selection_read_test("bp");
}
} // namespace memory_selection_test
#endif
