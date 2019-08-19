#include "openPMD/Streaming.hpp"

namespace openPMD
{
namespace chunk_assignment
{
    FirstPassByHostname::FirstPassByHostname(
        std::unique_ptr< SplitEgalitarian > _splitter )
        : splitter( std::move( _splitter ) )
    {
    }

    FirstPass::Result
    FirstPassByHostname::firstPass(
        ChunkTable const & table,
        RankMeta const & metaIn,
        RankMeta const & metaOut )
    {
        std::unordered_map< std::string, ChunkTable > chunkGroups;
        FirstPass::Result res;
        auto ranksPerHost = initRanksPerHost( metaOut );
        for( auto const & pair : table.chunkTable )
        {
            std::string hostname = metaIn[ pair.first ];
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

    std::unordered_map< std::string, std::list< int > >
    FirstPassByHostname::initRanksPerHost( RankMeta const & rankMeta )
    {
        std::unordered_map< std::string, std::list< int > > res;
        for( unsigned i = 0; i < rankMeta.size(); ++i )
        {
            auto & list = res[ rankMeta[ i ] ];
            list.emplace_back( i );
        }
        return res;
    }

    ChunkTable
    assignChunks(
        ChunkTable table,
        RankMeta const & rankIn,
        RankMeta const & rankOut,
        FirstPass & firstPass,
        SecondPass & secondPass )
    {
        FirstPass::Result intermediateAssignment =
            firstPass.firstPass( table, rankIn, rankOut );
        return secondPass.assignLeftovers( intermediateAssignment, rankIn, rankOut );
    }
} // namespace chunk_assignment
} // namespace openPMD