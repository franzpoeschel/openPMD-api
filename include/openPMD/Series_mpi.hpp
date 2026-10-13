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
#pragma once

#include "openPMD/Series_nompi.hpp"
#include "openPMD/config.hpp"

#if openPMD_HAVE_MPI
#include <mpi.h>
#endif

#include <string>

namespace openPMD
{
/**
 * MPI-enabled variant of the openPMD Series.
 *
 * This class adds the MPI communicator to the non-MPI `Series_nompi` base. The
 * communicator itself is stored type-erased in the base class' shared state, so
 * that this subclass does not add any data members. This keeps the memory
 * layout identical to `Series_nompi`, which is required for objects that cross
 * the library/consumer boundary (e.g. `ReadIterations`/`StatefulIterator`
 * store a `Series` by value).
 */
class Series_mpi : public Series_nompi
{
public:
    using Series_nompi::Series_nompi;

    Series_mpi() = default;
    Series_mpi(Series_mpi const &) = default;
    Series_mpi(Series_mpi &&) = default;
    Series_mpi &operator=(Series_mpi const &) = default;
    Series_mpi &operator=(Series_mpi &&) = default;

#if openPMD_HAVE_MPI
    /**
     * @brief Construct a new MPI-parallel Series.
     *
     * For further details, refer to the documentation of the non-MPI overload.
     *
     * @param filepath The file path.
     * @param at Access mode.
     * @param comm The MPI communicator.
     * @param options Advanced backend configuration via JSON.
     *      May be specified as a JSON-formatted string directly, or as a path
     *      to a JSON textfile, prepended by an at sign '@'.
     */
    Series_mpi(
        std::string const &filepath,
        Access at,
        MPI_Comm comm,
        std::string const &options = "{}");
#endif
};

static_assert(
    sizeof(Series_mpi) == sizeof(Series_nompi),
    "Series_mpi must not add any data members to Series_nompi: it is used "
    "interchangeably with Series_nompi across the library boundary.");
} // namespace openPMD
