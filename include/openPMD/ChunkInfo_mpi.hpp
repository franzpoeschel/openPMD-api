/* Copyright 2020-2025 Franz Poeschel, Axel Huebl
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

#include "openPMD/ChunkInfo_nompi.hpp"
#include "openPMD/config.hpp"

#if openPMD_HAVE_MPI
#include <mpi.h>

namespace openPMD
{
namespace host_info
{
    /**
     * @brief Retrieve the hostname information on all MPI ranks and distribute
     *        a map of "rank -> hostname" to all ranks.
     *
     * This call is MPI collective.
     *
     * @return chunk_assignment::RankMeta Hostname / processor name information
     *         for all MPI ranks known to the communicator.
     *         The result is returned on all ranks.
     */
    chunk_assignment::RankMeta byMethodCollective(MPI_Comm, Method);
} // namespace host_info
} // namespace openPMD
#endif
