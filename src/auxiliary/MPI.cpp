#if openPMD_HAVE_MPI

#    include "openPMD/auxiliary/MPI.hpp"

namespace openPMD
{
namespace auxiliary
{
    std::vector< std::string >
    collectStringsTo(
        MPI_Comm communicator,
        int destRank,
        std::string const & thisRankString )
    {
        int rank, size;
        MPI_Comm_rank( communicator, &rank );
        MPI_Comm_size( communicator, &size );
        int sendLength = thisRankString.size() + 1;

        int * sizesBuffer = nullptr;
        int * displs = nullptr;
        if( rank == destRank )
        {
            sizesBuffer = new int[ size ];
            displs = new int[ size ];
        }

        MPI_Gather(
            &sendLength,
            1,
            MPI_INT,
            sizesBuffer,
            1,
            MPI_INT,
            destRank,
            MPI_COMM_WORLD );

        char * namesBuffer = nullptr;
        if( rank == destRank )
        {
            size_t sum = 0;
            for( int i = 0; i < size; ++i )
            {
                displs[ i ] = sum;
                sum += sizesBuffer[ i ];
            }
            namesBuffer = new char[ sum ];
        }

        MPI_Gatherv(
            thisRankString.c_str(),
            sendLength,
            MPI_CHAR,
            namesBuffer,
            sizesBuffer,
            displs,
            MPI_CHAR,
            destRank,
            MPI_COMM_WORLD );

        if( rank == destRank )
        {
            std::vector< std::string > hostnames( size );
            for( int i = 0; i < size; ++i )
            {
                hostnames[ i ] = std::string( namesBuffer + displs[ i ] );
            }

            delete[] sizesBuffer;
            delete[] displs;
            delete[] namesBuffer;
            return hostnames;
        }
        else
        {
            return std::vector< std::string >();
        }
    }
} // namespace auxiliary
} // namespace openPMD

#endif