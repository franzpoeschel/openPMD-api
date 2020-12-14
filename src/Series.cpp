#include "openPMD/Series.hpp"

#include <iomanip>
#include <iostream>
#include <regex>
#include <set>

#include "openPMD/IO/AbstractIOHandlerHelper.hpp"
#include "openPMD/Series.tpp"
#include "openPMD/backend/Attributable.tpp"


namespace openPMD
{
template
class SeriesImpl< internal::SeriesInternal >;
template class SeriesImpl< Series >;
template class AttributableImpl< internal::SeriesInternal >;
template class AttributableImpl< Series >;

namespace internal
{
#if openPMD_HAVE_MPI
SeriesInternal::SeriesInternal(
    std::string const & filepath,
    Access at,
    MPI_Comm comm,
    std::string const & options )
{
    auto input = parseInput( filepath );
    auto handler =
        createIOHandler( input->path, at, input->format, comm, options );
    init( handler, std::move( input ) );
}
#endif

SeriesInternal::SeriesInternal(
    std::string const & filepath,
    Access at,
    std::string const & options )
{
    auto input = parseInput( filepath );
    auto handler = createIOHandler( input->path, at, input->format, options );
    init( handler, std::move( input ) );
}

SeriesInternal::~SeriesInternal()
{
    // we must not throw in a destructor
    try
    {
        flush();
    }
    catch( std::exception const & ex )
    {
        std::cerr << "[~Series] An error occurred: " << ex.what() << std::endl;
    }
    catch( ... )
    {
        std::cerr << "[~Series] An error occurred." << std::endl;
    }
}
} // namespace internal

#if openPMD_HAVE_MPI
Series::Series(
    std::string const & filepath,
    Access at,
    MPI_Comm comm,
    std::string const & options )
    : m_series{ std::make_shared< internal::SeriesInternal >(
          filepath,
          at,
          comm,
          options ) }
    , iterations{ m_series->iterations }
{
}
#endif

Series::Series(
    std::string const & filepath,
    Access at,
    std::string const & options )
    : m_series{ std::make_shared< internal::SeriesInternal >(
          filepath,
          at,
          options ) }
    , iterations{ m_series->iterations }
{
}

ReadIterations
Series::readIterations()
{
    return { *this };
}

WriteIterations
Series::writeIterations()
{
    internal::SeriesData & data = getSeries();
    if( !data.m_writeIterations->has_value() )
    {
        *data.m_writeIterations = WriteIterations( this->iterations );
    }
    return data.m_writeIterations->get();
}

SeriesIterator::SeriesIterator() : m_series()
{
}

SeriesIterator::SeriesIterator( Series series )
    : m_series( auxiliary::makeOption< Series >( std::move( series ) ) )
{
    auto it = m_series.get().iterations.begin();
    if( it == m_series.get().iterations.end() )
    {
        *this = end();
        return;
    }
    else
    {
        auto status = it->second.beginStep();
        if( status == AdvanceStatus::OVER )
        {
            *this = end();
            return;
        }
        it->second.setStepStatus( StepStatus::DuringStep );
    }
    m_currentIteration = it->first;
}

SeriesIterator &
SeriesIterator::operator++()
{
    if( !m_series.has_value() )
    {
        *this = end();
        return *this;
    }
    Series & series = m_series.get();
    auto & iterations = series.iterations;
    auto & currentIteration = iterations[ m_currentIteration ];
    if( !currentIteration.closed() )
    {
        currentIteration.close();
    }
    switch( series.iterationEncoding() )
    {
        using IE = IterationEncoding;
        case IE::groupBased:
        {
            // since we are in group-based iteration layout, it does not
            // matter which iteration we begin a step upon
            AdvanceStatus status = currentIteration.beginStep();
            if( status == AdvanceStatus::OVER )
            {
                *this = end();
                return *this;
            }
            currentIteration.setStepStatus( StepStatus::DuringStep );
            break;
        }
        default:
            break;
    }
    auto it = iterations.find( m_currentIteration );
    auto itEnd = iterations.end();
    if( it == itEnd )
    {
        *this = end();
        return *this;
    }
    ++it;
    if( it == itEnd )
    {
        *this = end();
        return *this;
    }
    m_currentIteration = it->first;
    switch( series.iterationEncoding() )
    {
        using IE = IterationEncoding;
        case IE::fileBased:
        {
            auto & iteration = series.iterations[ m_currentIteration ];
            AdvanceStatus status = iteration.beginStep();
            if( status == AdvanceStatus::OVER )
            {
                *this = end();
                return *this;
            }
            iteration.setStepStatus( StepStatus::DuringStep );
            break;
        }
        default:
            break;
    }
    return *this;
}

IndexedIteration
SeriesIterator::operator*()
{
    return IndexedIteration(
        m_series.get().iterations[ m_currentIteration ], m_currentIteration );
}

bool
SeriesIterator::operator==( SeriesIterator const & other ) const
{
    return this->m_currentIteration == other.m_currentIteration &&
        this->m_series.has_value() == other.m_series.has_value();
}

bool
SeriesIterator::operator!=( SeriesIterator const & other ) const
{
    return !operator==( other );
}

SeriesIterator
SeriesIterator::end()
{
    return {};
}

ReadIterations::ReadIterations( Series series )
    : m_series( std::move( series ) )
{
}

ReadIterations::iterator_t
ReadIterations::begin()
{
    return iterator_t{ m_series };
}

ReadIterations::iterator_t
ReadIterations::end()
{
    return SeriesIterator::end();
}

WriteIterations::SharedResources::SharedResources( iterations_t _iterations )
    : iterations( std::move( _iterations ) )
{
}

WriteIterations::SharedResources::~SharedResources()
{
    if( currentlyOpen.has_value() )
    {
        auto lastIterationIndex = currentlyOpen.get();
        auto & lastIteration = iterations.at( lastIterationIndex );
        if( !lastIteration.closed() )
        {
            lastIteration.close();
        }
    }
}

WriteIterations::WriteIterations( iterations_t iterations )
    : shared{ std::make_shared< SharedResources >( std::move( iterations ) ) }
{
}

WriteIterations::mapped_type &
WriteIterations::operator[]( key_type const & key )
{
    // make a copy
    // explicit cast so MSVC can figure out how to do it correctly
    return operator[]( static_cast< key_type && >( key_type{ key } ) );
}
WriteIterations::mapped_type &
WriteIterations::operator[]( key_type && key )
{
    if( shared->currentlyOpen.has_value() )
    {
        auto lastIterationIndex = shared->currentlyOpen.get();
        auto & lastIteration = shared->iterations.at( lastIterationIndex );
        if( lastIterationIndex != key && !lastIteration.closed() )
        {
            lastIteration.close();
        }
    }
    shared->currentlyOpen = key;
    auto & res = shared->iterations[ std::move( key ) ];
    if( res.getStepStatus() == StepStatus::NoStep )
    {
        res.beginStep();
        res.setStepStatus( StepStatus::DuringStep );
    }
    return res;
}
} // namespace openPMD