#include "openPMD/Streaming.hpp"

#include <iostream>
#include <map>
#include <unistd.h>

#include "openPMD/Dataset.hpp"

namespace openPMD
{
namespace chunk_assignment
{
    void
    SplitEgalitarianTrivial::splitEgalitarian(
        ChunkTable const & sourceChunks,
        std::list< int > const & destinationRanks,
        ChunkTable & sinkChunks )
    {
        auto it = destinationRanks.begin();
        auto nextRank = [&it, &destinationRanks]() {
            if( it == destinationRanks.end() )
            {
                it = destinationRanks.begin();
            }
            auto res = *it;
            it++;
            return res;
        };
        for( auto const & pair : sourceChunks.chunkTable )
        {
            for( auto chunk : pair.second )
            {
                sinkChunks.chunkTable[ nextRank() ].push_back(
                    std::move( chunk ) );
            }
        }
    }

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

        // which ranks live on host <string> in the sink?
        auto ranksPerHostSink = ranksPerHost( metaOut );
        for( auto & chunkGroup : chunkGroups )
        {
            // chunkGroup.first = source host
            // find reading ranks on the sink host with same name
            auto it = ranksPerHostSink.find( chunkGroup.first );
            if( it == ranksPerHostSink.end() || it->second.empty() )
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
        }
        return res;
    }

    SplitEgalitarianByCuboidSlice::SplitEgalitarianByCuboidSlice(
        std::unique_ptr< BlockSlicer > _blockSlicer,
        Extent _totalExtent,
        int _mpi_rank )
        : blockSlicer( std::move( _blockSlicer ) )
        , totalExtent( std::move( _totalExtent ) )
        , mpi_rank( _mpi_rank )
    {
    }

    void
    SplitEgalitarianByCuboidSlice::splitEgalitarian(
        ChunkTable const & sourceChunks,
        std::list< int > const & destinationRanks,
        ChunkTable & result )
    {
        // We calculate based on mpi_rank's position in destinationRanks
        auto mapped_size = destinationRanks.size();
        auto mapped_rank = mapped_size;
        {
            size_t i = 0;
            for( auto rank : destinationRanks )
            {
                if( rank == mpi_rank )
                {
                    mapped_rank = i;
                }
                ++i;
            }
        }
        if( mapped_rank == mapped_size )
        {
            std::cerr << "Rank " << mpi_rank
                      << " does not participate in chunk mapping." << std::endl;
        }
        auto & onMyRank = result.chunkTable[ mapped_rank ];
        Offset myOffset;
        Extent myExtent;
        std::tie( myOffset, myExtent ) =
            blockSlicer->sliceBlock( totalExtent, mapped_size, mapped_rank );

        for( auto const & perRank : sourceChunks.chunkTable )
        {
            for( auto chunk : perRank.second )
            {
                restrictToSelection(
                    chunk.first, chunk.second, myOffset, myExtent );
                for( auto ext : chunk.second )
                {
                    if( ext == 0 )
                    {
                        goto outer_loop;
                    }
                }
                onMyRank.push_back( std::move( chunk ) );
            outer_loop:;
            }
        }
    }


    std::unordered_map< std::string, std::list< int > >
    FirstPass::ranksPerHost( RankMeta const & rankMeta )
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
        return secondPass.assignLeftovers(
            intermediateAssignment, rankIn, rankOut );
    }

    SecondPassBySplitEgalitarian::SecondPassBySplitEgalitarian(
        std::unique_ptr< SplitEgalitarian > _splitter )
        : splitter( std::move( _splitter ) )
    {
    }

    ChunkTable
    SecondPassBySplitEgalitarian::assignLeftovers(
        FirstPass::Result intermediateResult,
        RankMeta const &,
        RankMeta const & out )
    {
        std::list< int > destinationRanks;
        for( size_t rank = 0; rank < out.size(); ++rank )
        {
            destinationRanks.push_back( rank );
        }
        splitter->splitEgalitarian(
            intermediateResult.leftOver,
            destinationRanks,
            intermediateResult.sinkSide );
        return intermediateResult.sinkSide;
    }

    FirstPassBySplitEgalitarian::FirstPassBySplitEgalitarian(
        std::unique_ptr< SplitEgalitarian > _splitter )
        : splitter( std::move( _splitter ) )
    {
    }

    FirstPass::Result
    FirstPassBySplitEgalitarian::firstPass(
        ChunkTable const & chunkTable,
        RankMeta const & /*in*/,
        RankMeta const & out )
    {
        std::list< int > destinationRanks;
        for( int i = 0; i < static_cast< int >( out.size() ); ++i )
        {
            destinationRanks.emplace_back( i );
        }
        Result res;
        splitter->splitEgalitarian(
            chunkTable, destinationRanks, res.sinkSide );
        return res;
    }

    FirstPassByCuboidSlice::FirstPassByCuboidSlice(
        std::unique_ptr< BlockSlicer > _blockSlicer,
        Extent _totalExtent,
        int _mpi_rank )
        : FirstPassBySplitEgalitarian( std::unique_ptr< SplitEgalitarian >(
              new SplitEgalitarianByCuboidSlice(
                  std::move( _blockSlicer ),
                  std::move( _totalExtent ),
                  _mpi_rank ) ) )
    {
    }
} // namespace chunk_assignment

namespace host_info
{
    constexpr size_t MAX_HOSTNAME_LENGTH = 200;

    std::string
    byMethod( Method method )
    {
        static std::map< Method, std::string ( * )() > map{ { Method::HOSTNAME,
                                                              &hostname } };
        return ( *map[ method ] )();
    }

    std::string
    hostname()
    {
        char hostname[ MAX_HOSTNAME_LENGTH ];
        gethostname( hostname, MAX_HOSTNAME_LENGTH );
        std::string res( hostname );
        return res;
    }
} // namespace host_info
} // namespace openPMD