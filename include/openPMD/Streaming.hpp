#pragma once

#include <list>
#include <string>
#include <utility>
#include <vector>

#include "openPMD/Dataset.hpp"
#include <unordered_map>

namespace openPMD
{
enum class AdvanceStatus
{
    OK,
    OVER
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
    using T_chunkTable = std::map< int, T_perRank >;

    T_chunkTable chunkTable;
};

using Chunk = ChunkTable::T_chunk;
using ChunkList = ChunkTable::T_perRank;

namespace chunk_assignment
{
    constexpr char const * HOSTFILE_VARNAME = "MPI_WRITTEN_HOSTFILE";

    using RankMeta = std::vector< std::string >;

    struct FirstPass
    {
        struct Result
        {
            ChunkTable sinkSide;
            ChunkTable leftOver;
        };

        static std::unordered_map< std::string, std::list< int > >
        ranksPerHost( RankMeta const & );

        virtual Result
        firstPass(
            ChunkTable const &,
            RankMeta const & in,
            RankMeta const & out ) = 0;
    };

    struct SecondPass
    {
        virtual ChunkTable
        assignLeftovers(
            FirstPass::Result,
            RankMeta const & in,
            RankMeta const & out ) = 0;
    };

    struct SplitEgalitarian
    {
        virtual void
        splitEgalitarian(
            ChunkTable const & sourceChunks,
            std::list< int > const & destinationRanks,
            ChunkTable & sinkChunks ) = 0;

        virtual ~SplitEgalitarian() = default;
    };


    ChunkTable
    assignChunks(
        ChunkTable,
        RankMeta const & rankMetaIn,
        RankMeta const & rankMetaOut,
        FirstPass & firstPass,
        SecondPass & secondPass );

    struct SplitEgalitarianTrivial : SplitEgalitarian
    {
        void
        splitEgalitarian(
            ChunkTable const & sourceChunks,
            std::list< int > const & destinationRanks,
            ChunkTable & sinkChunks ) override;
    };

    struct FirstPassByHostname : FirstPass
    {
        FirstPassByHostname( std::unique_ptr< SplitEgalitarian > );

        Result
        firstPass(
            ChunkTable const &,
            RankMeta const & in,
            RankMeta const & out ) override;

    private:
        std::unique_ptr< SplitEgalitarian > splitter;
    };

    struct SecondPassBySplitEgalitarian : SecondPass
    {
        SecondPassBySplitEgalitarian( std::unique_ptr< SplitEgalitarian > );

        ChunkTable
        assignLeftovers(
            FirstPass::Result,
            RankMeta const & in,
            RankMeta const & out ) override;

    private:
        std::unique_ptr< SplitEgalitarian > splitter;
    };
} // namespace chunk_assignment

namespace host_info
{
    std::string
    hostname();
}
} // namespace openPMD