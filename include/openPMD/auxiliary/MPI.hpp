#pragma once

#if openPMD_HAVE_MPI

#    include <mpi.h>
#    include <string>
#    include <vector>

namespace openPMD
{
namespace auxiliary
{
    std::vector< std::string >
    collectStringsTo(
        MPI_Comm communicator,
        int destRank,
        std::string const & thisRankString );

} // namespace auxiliary
} // namespace openPMD

#endif