#include "openPMD/Streaming.hpp"

namespace openPMD
{
namespace chunk_assignment
{
    FirstPassByHostname::FirstPassByHostname(
        RankMeta _hostnames,
        std::unique_ptr< SplitEgalitarian > _splitter )
        : splitter( std::move( _splitter ) )
        , hostnames( std::move( _hostnames ) )
    {
        initRanksPerHost();
    }

    FirstPass::Result
    FirstPassByHostname::firstPass(
        ChunkTable const & table,
        RankMeta const & metaInfo )
    {
        std::unordered_map< std::string, ChunkTable > chunkGroups;
        FirstPass::Result res;
        for( auto const & pair : table.chunkTable )
        {
            std::string hostname = metaInfo[ pair.first ];
            auto & hostTable = chunkGroups[ hostname ];
            for( auto chunk : pair.second )
            {
                hostTable.chunkTable[ pair.first ].emplace_back(
                    std::move( chunk ) );
            }
        }
        // chunkGroups will now contain chunks by hostname
        // the ranks are the source ranks
        for( auto & chunkGroup : chunkGroups )
        {
            // chunkGroup.first = source host
            // find reading ranks on the sink host with same name
            auto it = ranksPerHost.find( chunkGroup.first );
            if( it == ranksPerHost.end() || it->second.empty() )
            {
                for( auto & chunksPerRank : chunkGroup.second.chunkTable )
                {
                    res.leftOver.chunkTable[ chunksPerRank.first ] =
                        chunksPerRank.second;
                }
            }
            else
            {
                splitter->splitEgalitarian(
                    chunkGroup.second, it->second, res.sinkSide );
            }
            return res;
        }
        return res;
    }

    void
    FirstPassByHostname::initRanksPerHost()
    {
        for( unsigned i = 0; i < hostnames.size(); ++i )
        {
            auto & list = ranksPerHost[ hostnames[ i ] ];
            list.emplace_back( i );
        }
    }

    ChunkTable
    assignChunks(
        ChunkTable table,
        RankMeta const & rankMeta,
        FirstPass & firstPass,
        SecondPass & secondPass )
    {
        FirstPass::Result intermediateAssignment =
            firstPass.firstPass( table, rankMeta );
        return secondPass.assignLeftovers( intermediateAssignment, rankMeta );
    }
} // namespace chunk_assignment
} // namespace openPMD