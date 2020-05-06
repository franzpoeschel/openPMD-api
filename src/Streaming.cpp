#include "openPMD/Streaming.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <unistd.h>
#include <utility> // std::move

#include "openPMD/Dataset.hpp"

namespace openPMD
{
Chunk::Chunk( Offset _offset, Extent _extent, int _rank )
    : offset{ std::move( _offset ) },
      extent{ std::move( _extent ) },
      rank{ _rank }
{
}

std::vector< ChunkTable::T_sizedChunk >
ChunkTable::splitToSizeSorted( size_t maxSize ) const
{
    constexpr size_t dimension = 0;
    std::vector< T_sizedChunk > res;
    for( auto const & chunk : *this )
    {
        auto const & extent = chunk.extent;
        size_t sliceSize = 1;
        for( size_t i = 0; i < extent.size(); ++i )
        {
            if( i == dimension )
            {
                continue;
            }
            sliceSize *= extent[ i ];
        }
        if( sliceSize == 0 )
        {
            std::cerr << "Chunktable::splitToSizeSorted: encountered "
                            "zero-sized chunk" << std::endl;
            continue;
        }

        // this many slices go in one packet before it exceeds the max size
        size_t streakLength = maxSize / sliceSize;
        if( streakLength == 0 )
        {
            // otherwise we get caught in an endless loop
            ++streakLength;
        }
        size_t const slicedDimensionExtent = extent[ dimension ];

        for( size_t currentPosition = 0;; currentPosition += streakLength )
        {
            Chunk newChunk = chunk;
            newChunk.offset[ dimension ] += currentPosition;
            if( currentPosition + streakLength >= slicedDimensionExtent )
            {
                newChunk.extent[ dimension ] =
                    slicedDimensionExtent - currentPosition;
                size_t chunkSize = newChunk.extent[ dimension ] * sliceSize;
                res.emplace_back( std::move( newChunk ), chunkSize );
                break;
            }
            else
            {
                newChunk.extent[ dimension ] = streakLength;
                res.emplace_back(
                    std::move( newChunk ), streakLength * sliceSize );
            }
        }
    }
    std::sort(
        res.begin(),
        res.end(),
        []( T_sizedChunk const & left, T_sizedChunk const & right ) {
            return right.second < left.second; // decreasing order
        } );
    // for( auto const & chunk : res )
    // {
    //     std::cout << chunk.second << "\t";
    //     for( auto offs : chunk.first.offset )
    //     {
    //         std::cout << offs << ", ";
    //     }
    //     std::cout << "\t";
    //     for( auto ext : chunk.first.extent )
    //     {
    //         std::cout << ext << ", ";
    //     }
    //     std::cout << std::endl;
    // }
    return res;
}

namespace chunk_assignment
{
    namespace second_pass
    {
        ChunkTable &
        SliceIncomingChunks::assignLeftovers(
            ChunkTable const & sourceChunks,
            std::list< int > const & destinationRanks,
            ChunkTable & sinkChunks )
        {
            size_t totalExtent = 0;
            for( auto const & chunk : sourceChunks )
            {
                size_t chunkExtent = 1;
                for( auto ext : chunk.extent )
                {
                    chunkExtent *= ext;
                }
                totalExtent += chunkExtent;
            }
            size_t const idealSize = totalExtent / destinationRanks.size();
            std::vector< ChunkTable::T_sizedChunk > digestibleChunks =
                sourceChunks.splitToSizeSorted( idealSize );

            auto worker = [&destinationRanks,
                           &digestibleChunks,
                           &sinkChunks,
                           idealSize]() {
                for( auto destRank : destinationRanks )
                {
                    size_t leftoverSize = idealSize;
                    {
                        auto it = digestibleChunks.begin();
                        while( it != digestibleChunks.end() )
                        {
                            // should only be ==
                            // big chunks always come first in digestibleChunks
                            // so we need not check whether they fit
                            if( it->second >= idealSize )
                            {
                                it->first.rank = destRank;
                                sinkChunks.emplace_back(
                                    std::move( it->first ) );
                                digestibleChunks.erase( it );
                                break;
                            }
                            else if( it->second <= leftoverSize )
                            {
                                it->first.rank = destRank;
                                sinkChunks.emplace_back(
                                    std::move( it->first ) );
                                leftoverSize -= it->second;
                                it = digestibleChunks.erase( it );
                            }
                            else
                            {
                                ++it;
                            }
                        }
                    }
                }
            };

            // sic!
            // run the worker twice to implement a factor-two approximation
            // of the bin packing problem
            worker();
            worker();

            return sinkChunks;
        }


        ChunkTable &
        RoundRobin::assignLeftovers(
            ChunkTable const & sourceChunks,
            std::list< int > const & destinationRanks,
            ChunkTable & sinkChunks )
        {
            auto it = destinationRanks.begin();
            auto nextRank = [ &it, &destinationRanks ]() {
                if( it == destinationRanks.end() )
                {
                    it = destinationRanks.begin();
                }
                auto res = *it;
                it++;
                return res;
            };
            for( auto chunk : sourceChunks )
            {
                chunk.rank = nextRank();
                sinkChunks.push_back( std::move( chunk ) );
            }
            return sinkChunks;
        }

        SliceDataset::SliceDataset(
            std::unique_ptr< BlockSlicer > _blockSlicer,
            Extent _totalExtent,
            int _mpi_rank )
            : blockSlicer( std::move( _blockSlicer ) )
            , totalExtent( std::move( _totalExtent ) )
            , mpi_rank( _mpi_rank )
        {
        }

        ChunkTable &
        SliceDataset::assignLeftovers(
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
                          << " does not participate in chunk mapping."
                          << std::endl;
                return result;
            }
            Offset myOffset;
            Extent myExtent;
            std::tie( myOffset, myExtent ) = blockSlicer->sliceBlock(
                totalExtent, mapped_size, mapped_rank );

            for( auto chunk : sourceChunks )
            {
                restrictToSelection(
                    chunk.offset, chunk.extent, myOffset, myExtent );
                for( auto ext : chunk.extent )
                {
                    if( ext == 0 )
                    {
                        // poor man's labeled continue
                        goto outer_loop;
                    }
                }
                chunk.rank = mpi_rank;
                result.push_back( std::move( chunk ) );
            outer_loop:;
            }
            return result;
        }
    } // namespace second_pass

    namespace first_pass
    {
        FirstPass::Result
        Dummy::firstPass(
            ChunkTable const & table,
            RankMeta const &,
            RankMeta const & )
        {
            FirstPass::Result result;
            result.leftOver = table;
            return result;
        }

        ByHostname::ByHostname( std::unique_ptr< SecondPass > _splitter )
            : splitter( std::move( _splitter ) )
        {
        }

        FirstPass::Result
        ByHostname::firstPass(
            ChunkTable const & table,
            RankMeta const & metaIn,
            RankMeta const & metaOut )
        {
            std::unordered_map< std::string, ChunkTable > chunkGroups;
            FirstPass::Result res;
            for( auto chunk : table )
            {
                std::string hostname = metaIn[ chunk.rank ];
                auto & hostTable = chunkGroups[ hostname ];
                hostTable.emplace_back( std::move( chunk ) );
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
                    for( auto & chunk : chunkGroup.second )
                    {
                        res.leftOver.push_back( chunk );
                    }
                }
                else
                {
                    splitter->assignLeftovers(
                        chunkGroup.second, it->second, res.sinkSide );
                }
            }
            return res;
        }
    } // namespace first_pass

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
        std::list< int > destRanks;
        for( unsigned int rank = 0; rank < rankOut.size(); ++rank )
        {
            destRanks.push_back( rank );
        }
        secondPass.assignLeftovers(
            intermediateAssignment.leftOver,
            destRanks,
            intermediateAssignment.sinkSide );
        return intermediateAssignment.sinkSide;
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