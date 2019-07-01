#pragma once

#include <list>
#include <vector>
#include <utility>

#include "openPMD/Dataset.hpp"

namespace openPMD
{
enum class AdvanceStatus
{
    OK, OVER
};

enum class AdvanceMode
{
    AUTO, // according to accesstype
    READ,
    WRITE
};

struct ChunkTable
{
    using T_chunk = std::pair< Offset, Extent >;
    using T_perRank = std::list< T_chunk >;
    using T_chunkTable = std::vector< T_perRank >;
    
    T_chunkTable chunkTable;
    
    ChunkTable( size_t n = 0 ):
        chunkTable( n )
    {}
};
} // namespace openPMD