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

namespace chunk_assignment
{
    using RankMeta = std::vector< std::string >;

    struct FirstPass
    {
        struct Result
        {
            ChunkTable sinkSide;
            ChunkTable leftOver;
        };

        virtual Result
        firstPass( ChunkTable const &, RankMeta const & ) = 0;
    };

    struct SecondPass
    {
        virtual ChunkTable
        assignLeftovers( FirstPass::Result, RankMeta const & ) = 0;
    };

    struct SplitEgalitarian
    {
        virtual void
        splitEgalitarian(
            ChunkTable const & sourceChunks,
            std::list< int > destinationRanks,
            ChunkTable & sinkChunks ) = 0;

        virtual ~SplitEgalitarian() = default;
    };


    ChunkTable
    assignChunks(
        ChunkTable,
        RankMeta const &,
        FirstPass & firstPass,
        SecondPass & secondPass );

    struct FirstPassByHostname : FirstPass
    {
        FirstPassByHostname(
            RankMeta hostnames,
            std::unique_ptr< SplitEgalitarian > );

        Result
        firstPass( ChunkTable const &, RankMeta const & ) override;

    private:
        std::unique_ptr< SplitEgalitarian > splitter;
        RankMeta const hostnames;
        std::unordered_map< std::string, std::list< int > > ranksPerHost;

        void
        initRanksPerHost();
    };
} // namespace chunk_assignment

} // namespace openPMD