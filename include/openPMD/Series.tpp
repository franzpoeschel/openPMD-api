#include <iomanip>
#include <iostream>
#include <regex>
#include <set>

#include "openPMD/Series.hpp"
#include "openPMD/IO/AbstractIOHandlerHelper.hpp"
#include "openPMD/auxiliary/Date.hpp"
#include "openPMD/auxiliary/Filesystem.hpp"
#include "openPMD/auxiliary/StringManip.hpp"

namespace openPMD
{
using SeriesData = internal::SeriesData;

namespace
{
    /** Remove the filename extension of a given storage format.
     *
     * @param   filename    String containing the filename, possibly with filename extension.
     * @param   f           File format to remove filename extension for.
     * @return  String containing the filename without filename extension.
     */
    std::string cleanFilename(std::string const &filename, Format f);

    /** Create a functor to determine if a file can be of a format and matches an iterationEncoding, given the filename on disk.
     *
     * @param   prefix      String containing head (i.e. before %T) of desired filename without filename extension.
     * @param   padding     Amount of padding allowed in iteration number %T. If zero, any amount of padding is matched.
     * @param   postfix     String containing tail (i.e. after %T) of desired filename without filename extension.
     * @param   f           File format to check backend applicability for.
     * @return  Functor returning tuple of bool and int.
     *          bool is True if file could be of type f and matches the iterationEncoding. False otherwise.
     *          int is the amount of padding present in the iteration number %T. Is 0 if bool is False.
     */
    std::function<std::tuple<bool, int>(std::string const &)>
    matcher(std::string const &prefix, int padding, std::string const &postfix, Format f);
} // namespace [anonymous]

template< typename SeriesWrapper >
struct SeriesImpl< SeriesWrapper >::ParsedInput
{
    std::string path;
    std::string name;
    Format format;
    IterationEncoding iterationEncoding;
    std::string filenamePrefix;
    std::string filenamePostfix;
    int filenamePadding;
};  //ParsedInput

template< typename SeriesWrapper >
std::string
SeriesImpl< SeriesWrapper >::openPMD() const
{
    return this->getAttribute( "openPMD" )
        .template get< std::string >();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setOpenPMD(std::string const& o)
{
    this->setAttribute("openPMD", o);
    return *this;
}

template<typename SeriesWrapper>
uint32_t SeriesImpl<SeriesWrapper>::openPMDextension() const
{
    return this->getAttribute("openPMDextension").template get<uint32_t>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setOpenPMDextension(uint32_t oe)
{
    this->setAttribute("openPMDextension", oe);
    return *this;
}


template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::basePath() const
{
    return this->getAttribute("basePath").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setBasePath(std::string const& bp)
{
    std::string version = openPMD();
    if(version == "1.0.0" || version == "1.0.1" || version == "1.1.0")
        throw std::runtime_error("Custom basePath not allowed in openPMD <=1.1.0");

    this->setAttribute("basePath", bp);
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::meshesPath() const
{
    return this->getAttribute("meshesPath").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setMeshesPath(std::string const& mp)
{
    if(std::any_of(
            get().iterations.begin(),
            get().iterations.end(),
            [](Container<Iteration, uint64_t>::value_type const& i) { return i.second.meshes.written(); }))
        throw std::runtime_error("A files meshesPath can not (yet) be changed "
                                    "after it has been written.");

    if(auxiliary::ends_with(mp, '/'))
        this->setAttribute("meshesPath", mp);
    else
        this->setAttribute("meshesPath", mp + "/");
    this->dirty() = true;
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::particlesPath() const
{
    return this->getAttribute("particlesPath").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setParticlesPath(std::string const& pp)
{
    if(std::any_of(
            get().iterations.begin(),
            get().iterations.end(),
            [](Container<Iteration, uint64_t>::value_type const& i) { return i.second.particles.written(); }))
        throw std::runtime_error("A files particlesPath can not (yet) be "
                                    "changed after it has been written.");

    if(auxiliary::ends_with(pp, '/'))
        this->setAttribute("particlesPath", pp);
    else
        this->setAttribute("particlesPath", pp + "/");
    this->dirty() = true;
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::author() const
{
    return this->getAttribute("author").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setAuthor(std::string const& a)
{
    this->setAttribute("author", a);
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::software() const
{
    return this->getAttribute("software").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setSoftware(
    std::string const& newName,
    std::string const& newVersion)
{
    this->setAttribute("software", newName);
    this->setAttribute("softwareVersion", newVersion);
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::softwareVersion() const
{
    return this->getAttribute("softwareVersion").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setSoftwareVersion(std::string const& sv)
{
    this->setAttribute("softwareVersion", sv);
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::date() const
{
    return this->getAttribute("date").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setDate(std::string const& d)
{
    this->setAttribute("date", d);
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::softwareDependencies() const
{
    return this->getAttribute("softwareDependencies").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setSoftwareDependencies(
    std::string const& newSoftwareDependencies)
{
    this->setAttribute("softwareDependencies", newSoftwareDependencies);
    return *this;
}

template<typename SeriesWrapper>
std::string SeriesImpl<SeriesWrapper>::machine() const
{
    return this->getAttribute("machine").template get<std::string>();
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setMachine(std::string const& newMachine)
{
    this->setAttribute("machine", newMachine);
    return *this;
}

template<typename SeriesWrapper>
IterationEncoding SeriesImpl<SeriesWrapper>::iterationEncoding() const
{
    return *get().m_iterationEncoding;
}

template<typename SeriesWrapper>
SeriesImpl<SeriesWrapper>& SeriesImpl<SeriesWrapper>::setIterationEncoding(IterationEncoding ie)
{
    if(this->written())
        throw std::runtime_error("A files iterationEncoding can not (yet) be "
                                    "changed after it has been written.");

    *get().m_iterationEncoding = ie;
    switch(ie)
    {
        case IterationEncoding::fileBased:
            setIterationFormat( *get().m_name );
            this->setAttribute(
                "iterationEncoding", std::string( "fileBased" ) );
            break;
        case IterationEncoding::groupBased:
            setIterationFormat( get().BASEPATH );
            this->setAttribute(
                "iterationEncoding", std::string( "groupBased" ) );
            break;
    }
    return *this;
}

template< typename SeriesWrapper >
std::string
SeriesImpl< SeriesWrapper >::iterationFormat() const
{
    return
        this->getAttribute( "iterationFormat" ).template get< std::string >();
}

template< typename SeriesWrapper >
SeriesImpl< SeriesWrapper > &
SeriesImpl< SeriesWrapper >::setIterationFormat( std::string const & i )
{
    if( this->written() )
        throw std::runtime_error( "A files iterationFormat can not (yet) be "
                                  "changed after it has been written." );

    if( *get().m_iterationEncoding == IterationEncoding::groupBased )
        if( basePath() != i &&
            ( openPMD() == "1.0.1" || openPMD() == "1.0.0" ) )
            throw std::invalid_argument(
                "iterationFormat must not differ from basePath " + basePath() +
                " for groupBased data" );

    this->setAttribute( "iterationFormat", i );
    return *this;
}

template< typename SeriesWrapper >
std::string
SeriesImpl< SeriesWrapper >::name() const
{
    return *get().m_name;
}

template< typename SeriesWrapper >
SeriesImpl< SeriesWrapper > &
SeriesImpl< SeriesWrapper >::setName( std::string const & n )
{
    if( this->written() )
        throw std::runtime_error( "A files name can not (yet) be changed after "
                                  "it has been written." );

    if( *get().m_iterationEncoding == IterationEncoding::fileBased &&
        !auxiliary::contains( *get().m_name, "%T" ) )
        throw std::runtime_error( "For fileBased formats the iteration regex "
                                  "%T must be included in the file name" );

    *get().m_name = n;
    this->dirty() = true;
    return *this;
}

template< typename SeriesWrapper >
std::string
SeriesImpl< SeriesWrapper >::backend() const
{
    internal::AttributableData const & attri = attributable_t::get();
    return attri.IOHandler->backendName();
}

template<typename SeriesWrapper>
void SeriesImpl<SeriesWrapper>::flush()
{
    SeriesData& series = get();
    flush_impl(series.iterations.begin(), series.iterations.end());
}

template<typename SeriesWrapper>
std::unique_ptr< typename SeriesImpl<SeriesWrapper>::ParsedInput >
SeriesImpl<SeriesWrapper>::parseInput(std::string filepath)
{
    std::unique_ptr< ParsedInput > input{new ParsedInput};

#ifdef _WIN32
    if( auxiliary::contains(filepath, '/') )
    {
        std::cerr << "Filepaths on WINDOWS platforms may not contain slashes '/'! "
                  << "Replacing with backslashes '\\' unconditionally!" << std::endl;
        filepath = auxiliary::replace_all(filepath, "/", "\\");
    }
#else
    if( auxiliary::contains(filepath, '\\') )
    {
        std::cerr << "Filepaths on UNIX platforms may not include backslashes '\\'! "
                  << "Replacing with slashes '/' unconditionally!" << std::endl;
        filepath = auxiliary::replace_all(filepath, "\\", "/");
    }
#endif
    auto const pos = filepath.find_last_of(auxiliary::directory_separator);
    if( std::string::npos == pos )
    {
        input->path = ".";
        input->path.append(1, auxiliary::directory_separator);
        input->name = filepath;
    }
    else
    {
        input->path = filepath.substr(0, pos + 1);
        input->name = filepath.substr(pos + 1);
    }

    input->format = determineFormat(input->name);

    std::regex pattern("(.*)%(0[[:digit:]]+)?T(.*)");
    std::smatch regexMatch;
    std::regex_match(input->name, regexMatch, pattern);
    if( regexMatch.empty() )
        input->iterationEncoding = IterationEncoding::groupBased;
    else if( regexMatch.size() == 4 )
    {
        input->iterationEncoding = IterationEncoding::fileBased;
        input->filenamePrefix = regexMatch[1].str();
        std::string const& pad = regexMatch[2];
        if( pad.empty() )
            input->filenamePadding = 0;
        else
        {
            if( pad.front() != '0' )
                throw std::runtime_error("Invalid iterationEncoding " + input->name);
            input->filenamePadding = std::stoi(pad);
        }
        input->filenamePostfix = regexMatch[3].str();
    } else
        throw std::runtime_error("Can not determine iterationFormat from filename " + input->name);

    input->filenamePostfix = cleanFilename(input->filenamePostfix, input->format);

    input->name = cleanFilename(input->name, input->format);

    return input;
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::init(
    std::shared_ptr< AbstractIOHandler > ioHandler,
    std::unique_ptr< SeriesImpl< SeriesWrapper >::ParsedInput > input )
{
    SeriesData & series = get();
    internal::AttributableData & attri = attributable_t::get();
    attri.m_writable->IOHandler = ioHandler;
    attri.IOHandler = attri.m_writable->IOHandler.get();
    series.iterations.linkHierarchy(attri.m_writable);

    series.m_name = std::make_shared< std::string >(input->name);

    series.m_format = std::make_shared< Format >(input->format);

    series.m_filenamePrefix = std::make_shared< std::string >(input->filenamePrefix);
    series.m_filenamePostfix = std::make_shared< std::string >(input->filenamePostfix);
    series.m_filenamePadding = std::make_shared< int >(input->filenamePadding);

    if(attri.IOHandler->m_frontendAccess == Access::READ_ONLY || attri.IOHandler->m_frontendAccess == Access::READ_WRITE )
    {
        /* Allow creation of values in Containers and setting of Attributes
         * Would throw for Access::READ_ONLY */
        auto oldType = attri.IOHandler->m_frontendAccess;
        auto newType = const_cast< Access* >(&attri.m_writable->IOHandler->m_frontendAccess);
        *newType = Access::READ_WRITE;

        if( input->iterationEncoding == IterationEncoding::fileBased )
            readFileBased();
        else
            readGroupBased();

        if( series.iterations.empty() )
        {
            /* Access::READ_WRITE can be used to create a new Series
             * allow setting attributes in that case */
            this->written() = false;

            initDefaults();
            setIterationEncoding(input->iterationEncoding);

            this->written() = true;
        }

        *newType = oldType;
    } else
    {
        initDefaults();
        setIterationEncoding(input->iterationEncoding);
    }
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::initDefaults()
{
    if( !this->containsAttribute("openPMD"))
        setOpenPMD( getStandard() );
    if( !this->containsAttribute("openPMDextension"))
        setOpenPMDextension(0);
    if( !this->containsAttribute("basePath"))
        this->setAttribute("basePath", std::string(get().BASEPATH));
    if( !this->containsAttribute("date"))
        setDate( auxiliary::getDateString() );
    if( !this->containsAttribute("software"))
        setSoftware( "openPMD-api", getVersion() );
}

template< typename SeriesWrapper >
std::future< void >
SeriesImpl< SeriesWrapper >::flush_impl(
    iterations_iterator begin,
    iterations_iterator end )
{
    switch( *get().m_iterationEncoding )
    {
        using IE = IterationEncoding;
        case IE::fileBased:
            flushFileBased( begin, end );
            break;
        case IE::groupBased:
            flushGroupBased( begin, end );
            break;
    }

    return attributable_t::get().IOHandler->flush();
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::flushFileBased(
    iterations_iterator begin, iterations_iterator end )
{
    internal::SeriesData & series = get();
    internal::AttributableData & attri = attributable_t::get();
    if( end == begin )
        throw std::runtime_error(
            "fileBased output can not be written with no iterations." );

    if( attri.IOHandler->m_frontendAccess == Access::READ_ONLY )
        for( auto it = begin; it != end; ++it )
        {
            bool const dirtyRecursive = it->second.dirtyRecursive();
            if( *it->second.m_closed
                == Iteration::CloseStatus::ClosedInBackend )
            {
                // file corresponding with the iteration has previously been
                // closed and fully flushed
                // verify that there have been no further accesses
                if( dirtyRecursive )
                {
                    throw std::runtime_error(
                        "[Series] Detected illegal access to iteration that "
                        "has been closed previously." );
                }
                continue;
            }
            /*
             * Opening a file is expensive, so let's do it only if necessary.
             * Necessary if:
             * 1. The iteration itself has been changed somewhere.
             * 2. Or the Series has been changed globally in a manner that
             *    requires adapting all iterations.
             */
            if( dirtyRecursive || this->dirty() )
            {
                // openIteration() will update the close status
                openIteration( it->first, it->second );
                it->second.flush();
            }

            if( *it->second.m_closed
                == Iteration::CloseStatus::ClosedInFrontend )
            {
                Parameter< Operation::CLOSE_FILE > fClose;
                attri.IOHandler->enqueue(
                    IOTask( &it->second, std::move( fClose ) ) );
                *it->second.m_closed = Iteration::CloseStatus::ClosedInBackend;
            }
            attri.IOHandler->flush();
        }
    else
    {
        bool allDirty = this->dirty();
        for( auto it = begin; it != end; ++it )
        {
            bool const dirtyRecursive = it->second.dirtyRecursive();
            if( *it->second.m_closed
                == Iteration::CloseStatus::ClosedInBackend )
            {
                // file corresponding with the iteration has previously been
                // closed and fully flushed
                // verify that there have been no further accesses
                if (!it->second.written())
                {
                    throw std::runtime_error(
                        "[Series] Closed iteration has not been written. This "
                        "is an internal error." );
                }
                if( dirtyRecursive )
                {
                    throw std::runtime_error(
                        "[Series] Detected illegal access to iteration that "
                        "has been closed previously." );
                }
                continue;
            }

            /*
             * Opening a file is expensive, so let's do it only if necessary.
             * Necessary if:
             * 1. The iteration itself has been changed somewhere.
             * 2. Or the Series has been changed globally in a manner that
             *    requires adapting all iterations.
             */
            if( dirtyRecursive || this->dirty() )
            {
                /* as there is only one series,
                * emulate the file belonging to each iteration as not yet written
                */
                this->written() = false;
                series.iterations.written() = false;

                this->dirty() |= it->second.dirty();
                std::string filename = iterationFilename( it->first );
                it->second.flushFileBased(filename, it->first);

                series.iterations.flush(
                    auxiliary::replace_first(basePath(), "%T/", ""));

                this->flushAttributes();

                switch( *it->second.m_closed )
                {
                    using CL = Iteration::CloseStatus;
                    case CL::Open:
                    case CL::ClosedTemporarily:
                        *it->second.m_closed = CL::Open;
                        break;
                    default:
                        // keep it
                        break;
                }
            }

            if( *it->second.m_closed ==
                Iteration::CloseStatus::ClosedInFrontend )
            {
                Parameter< Operation::CLOSE_FILE > fClose;
                attri.IOHandler->enqueue(
                    IOTask( &it->second, std::move( fClose ) ) );
                *it->second.m_closed = Iteration::CloseStatus::ClosedInBackend;
            }

            attri.IOHandler->flush();

            /* reset the dirty bit for every iteration (i.e. file)
             * otherwise only the first iteration will have updates attributes */
            this->dirty() = allDirty;
        }
        this->dirty() = false;
    }
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::flushGroupBased(
    iterations_iterator begin, iterations_iterator end )
{
    internal::SeriesData & series = get();
    internal::AttributableData & attri = attributable_t::get();

    if( attri.IOHandler->m_frontendAccess == Access::READ_ONLY )
        for( auto it = begin; it != end; ++it )
        {
            if( *it->second.m_closed ==
                Iteration::CloseStatus::ClosedInBackend )
            {
                // file corresponding with the iteration has previously been
                // closed and fully flushed
                // verify that there have been no further accesses
                if( it->second.dirtyRecursive() )
                {
                    throw std::runtime_error(
                        "[Series] Illegal access to iteration " +
                        std::to_string( it->first ) +
                        " that has been closed previously." );
                }
                continue;
            }
            it->second.flush();
            if( *it->second.m_closed == Iteration::CloseStatus::ClosedInFrontend )
            {
                // the iteration has no dedicated file in group-based mode
                *it->second.m_closed = Iteration::CloseStatus::ClosedInBackend;
            }
            attri.IOHandler->flush();
        }
    else
    {
        if( !this->written() )
        {
            Parameter< Operation::CREATE_FILE > fCreate;
            fCreate.name = *get().m_name;
            // @todo IOTask(this, ...)
            attri.IOHandler->enqueue(IOTask(attri.m_writable.get(), fCreate));
        }

        series.iterations.flush(
            auxiliary::replace_first(basePath(), "%T/", ""));

        for( auto it = begin; it != end; ++it )
        {
            if( *it->second.m_closed ==
                Iteration::CloseStatus::ClosedInBackend )
            {
                // file corresponding with the iteration has previously been
                // closed and fully flushed
                // verify that there have been no further accesses
                if (!it->second.written())
                {
                    throw std::runtime_error(
                        "[Series] Closed iteration has not been written. This "
                        "is an internal error." );
                }
                if( it->second.dirtyRecursive() )
                {
                    throw std::runtime_error(
                        "[Series] Illegal access to iteration " +
                        std::to_string( it->first ) +
                        " that has been closed previously." );
                }
                continue;
            }
            if( !it->second.written() )
            {
                it->second.m_writable->parent = getWritable(
                    &series.iterations );
                it->second.parent = getWritable( &series.iterations );
            }
            it->second.flushGroupBased(it->first);
            if( *it->second.m_closed == Iteration::CloseStatus::ClosedInFrontend )
            {
                // the iteration has no dedicated file in group-based mode
                *it->second.m_closed = Iteration::CloseStatus::ClosedInBackend;
            }
        }

        this->flushAttributes();
        attri.IOHandler->flush();
    }
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::flushMeshesPath()
{
    internal::AttributableData & attri = attributable_t::get();
    Parameter< Operation::WRITE_ATT > aWrite;
    aWrite.name = "meshesPath";
    Attribute a = this->getAttribute("meshesPath");
    aWrite.resource = a.getResource();
    aWrite.dtype = a.dtype;
    attri.IOHandler->enqueue(IOTask(attri.m_writable.get(), aWrite));
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::flushParticlesPath()
{
    internal::AttributableData & attri = attributable_t::get();
    Parameter< Operation::WRITE_ATT > aWrite;
    aWrite.name = "particlesPath";
    Attribute a = this->getAttribute("particlesPath");
    aWrite.resource = a.getResource();
    aWrite.dtype = a.dtype;
    attri.IOHandler->enqueue(IOTask(attri.m_writable.get(), aWrite));
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::readFileBased( )
{
    internal::AttributableData & attri = attributable_t::get();
    SeriesData & series = get();
    Writable * writable = attri.m_writable.get();

    Parameter< Operation::OPEN_FILE > fOpen;
    Parameter< Operation::CLOSE_FILE > fClose;
    Parameter< Operation::READ_ATT > aRead;

    if( !auxiliary::directory_exists(attri.IOHandler->directory) )
        throw no_such_file_error(
            "Supplied directory is not valid: " + attri.IOHandler->directory);

    auto isPartOfSeries = matcher(
        *series.m_filenamePrefix, *series.m_filenamePadding,
        *series.m_filenamePostfix, *series.m_format);
    bool isContained;
    int padding;
    std::set< int > paddings;
    for( auto const& entry : auxiliary::list_directory(attri.IOHandler->directory) )
    {
        std::tie(isContained, padding) = isPartOfSeries(entry);
        if( isContained )
        {
            // TODO skip if the padding is exact the number of chars in an iteration?
            paddings.insert(padding);

            fOpen.name = entry;
            attri.IOHandler->enqueue(IOTask(writable, fOpen));
            attri.IOHandler->flush();
            series.iterations.m_writable->parent = writable;
            series.iterations.parent = writable;

            readBase();

            using DT = Datatype;
            aRead.name = "iterationEncoding";
            attri.IOHandler->enqueue( IOTask( writable, aRead ) );
            attri.IOHandler->flush();
            if( *aRead.dtype == DT::STRING )
            {
                std::string encoding =
                    Attribute( *aRead.resource ).get< std::string >();
                if( encoding == "fileBased" )
                    *series.m_iterationEncoding = IterationEncoding::fileBased;
                else if( encoding == "groupBased" )
                {
                    *series.m_iterationEncoding = IterationEncoding::groupBased;
                    std::cerr << "Series constructor called with iteration "
                                    "regex '%T' suggests loading a "
                                << "time series with fileBased iteration "
                                    "encoding. Loaded file is groupBased.\n";
                }
                else
                    throw std::runtime_error(
                        "Unknown iterationEncoding: " + encoding );
                this->setAttribute( "iterationEncoding", encoding );
            }
            else
                throw std::runtime_error( "Unexpected Attribute datatype "
                                            "for 'iterationEncoding'" );

            aRead.name = "iterationFormat";
            attri.IOHandler->enqueue( IOTask( writable, aRead ) );
            attri.IOHandler->flush();
            if( *aRead.dtype == DT::STRING )
            {
                this->written() = false;
                setIterationFormat(
                    Attribute( *aRead.resource ).get< std::string >() );
                this->written() = true;
            }
            else
                throw std::runtime_error(
                    "Unexpected Attribute datatype for 'iterationFormat'" );

            read();
            attri.IOHandler->enqueue(IOTask(writable, fClose));
            attri.IOHandler->flush();
        }
    }

    for( auto & iteration : series.iterations )
    {
        *iteration.second.m_closed = Iteration::CloseStatus::ClosedTemporarily;
    }

    if( series.iterations.empty() )
    {
        /* Frontend access type might change during Series::read() to allow parameter modification.
         * Backend access type stays unchanged for the lifetime of a Series. */
        if(attri.IOHandler->m_backendAccess == Access::READ_ONLY  )
            throw std::runtime_error("No matching iterations found: " + name());
        else
            std::cerr << "No matching iterations found: " << name() << std::endl;
    }

    if( paddings.size() == 1u )
        *series.m_filenamePadding = *paddings.begin();

    /* Frontend access type might change during Series::read() to allow parameter modification.
     * Backend access type stays unchanged for the lifetime of a Series. */
    if( paddings.size() > 1u && attri.IOHandler->m_backendAccess == Access::READ_WRITE )
        throw std::runtime_error("Cannot write to a series with inconsistent iteration padding. "
                                 "Please specify '%0<N>T' or open as read-only.");
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::readGroupBased( bool do_init )
{
    internal::AttributableData & attri = attributable_t::get();
    SeriesData & series = get();
    Writable * writable = attri.m_writable.get();

    Parameter< Operation::OPEN_FILE > fOpen;
    fOpen.name = *series.m_name;
    attri.IOHandler->enqueue(IOTask(writable, fOpen));
    attri.IOHandler->flush();

    if( do_init )
    {
        readBase();

        using DT = Datatype;
        Parameter< Operation::READ_ATT > aRead;
        aRead.name = "iterationEncoding";
        attri.IOHandler->enqueue(IOTask(writable, aRead));
        attri.IOHandler->flush();
        if( *aRead.dtype == DT::STRING )
        {
            std::string encoding = Attribute(*aRead.resource).get< std::string >();
            if( encoding == "groupBased" )
                *series.m_iterationEncoding = IterationEncoding::groupBased;
            else if( encoding == "fileBased" )
            {
                *series.m_iterationEncoding = IterationEncoding::fileBased;
                std::cerr << "Series constructor called with explicit iteration suggests loading a "
                          << "single file with groupBased iteration encoding. Loaded file is fileBased.\n";
            } else
                throw std::runtime_error("Unknown iterationEncoding: " + encoding);
            this->setAttribute("iterationEncoding", encoding);
        }
        else
            throw std::runtime_error("Unexpected Attribute datatype for 'iterationEncoding'");

        aRead.name = "iterationFormat";
        attri.IOHandler->enqueue(IOTask(writable, aRead));
        attri.IOHandler->flush();
        if( *aRead.dtype == DT::STRING )
        {
            this->written() = false;
            setIterationFormat(Attribute(*aRead.resource).get< std::string >());
            this->written() = true;
        }
        else
            throw std::runtime_error("Unexpected Attribute datatype for 'iterationFormat'");
    }

    read();
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::readBase()
{
    internal::AttributableData & attri = attributable_t::get();
    SeriesData & series = get();
    Writable * writable = attri.m_writable.get();

    using DT = Datatype;
    Parameter< Operation::READ_ATT > aRead;

    aRead.name = "openPMD";
    attri.IOHandler->enqueue(IOTask(writable, aRead));
    attri.IOHandler->flush();
    if( *aRead.dtype == DT::STRING )
        setOpenPMD(Attribute(*aRead.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'openPMD'");

    aRead.name = "openPMDextension";
    attri.IOHandler->enqueue(IOTask(writable, aRead));
    attri.IOHandler->flush();
    if( *aRead.dtype == determineDatatype< uint32_t >() )
        setOpenPMDextension(Attribute(*aRead.resource).get< uint32_t >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'openPMDextension'");

    aRead.name = "basePath";
    attri.IOHandler->enqueue(IOTask(writable, aRead));
    attri.IOHandler->flush();
    if( *aRead.dtype == DT::STRING )
        this->setAttribute(
            "basePath", Attribute(*aRead.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'basePath'");

    Parameter< Operation::LIST_ATTS > aList;
    attri.IOHandler->enqueue(IOTask(writable, aList));
    attri.IOHandler->flush();
    if( std::count(aList.attributes->begin(), aList.attributes->end(), "meshesPath") == 1 )
    {
        aRead.name = "meshesPath";
        attri.IOHandler->enqueue(IOTask(writable, aRead));
        attri.IOHandler->flush();
        if( *aRead.dtype == DT::STRING )
        {
            /* allow setting the meshes path after completed IO */
            for( auto& it : series.iterations )
                it.second.meshes.written() = false;

            setMeshesPath(Attribute(*aRead.resource).get< std::string >());

            for( auto& it : series.iterations )
                it.second.meshes.written() = true;
        }
        else
            throw std::runtime_error("Unexpected Attribute datatype for 'meshesPath'");
    }

    if( std::count(aList.attributes->begin(), aList.attributes->end(), "particlesPath") == 1 )
    {
        aRead.name = "particlesPath";
        attri.IOHandler->enqueue(IOTask(writable, aRead));
        attri.IOHandler->flush();
        if( *aRead.dtype == DT::STRING )
        {
            /* allow setting the meshes path after completed IO */
            for( auto& it : series.iterations )
                it.second.particles.written() = false;

            setParticlesPath(Attribute(*aRead.resource).get< std::string >());


            for( auto& it : series.iterations )
                it.second.particles.written() = true;
        }
        else
            throw std::runtime_error("Unexpected Attribute datatype for 'particlesPath'");
    }
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::read()
{
    internal::AttributableData & attri = attributable_t::get();
    Parameter< Operation::OPEN_PATH > pOpen;
    std::string version = openPMD();
    if( version == "1.0.0" || version == "1.0.1" || version == "1.1.0" )
        pOpen.path = auxiliary::replace_first(basePath(), "/%T/", "");
    else
        throw std::runtime_error("Unknown openPMD version - " + version);
    attri.IOHandler->enqueue(IOTask(&get().iterations, pOpen));

    this->readAttributes();
    get().iterations.readAttributes();

    /* obtain all paths inside the basepath (i.e. all iterations) */
    Parameter< Operation::LIST_PATHS > pList;
    attri.IOHandler->enqueue(IOTask(&get().iterations, pList));
    attri.IOHandler->flush();

    for( auto const& it : *pList.paths )
    {
        Iteration& i = get().iterations[std::stoull(it)];
        if ( i.closedByWriter( ) )
        {
            continue;
        }
        pOpen.path = it;
        attri.IOHandler->enqueue(IOTask(&i, pOpen));
        i.read();
    }
}

template< typename SeriesWrapper >
std::string
SeriesImpl< SeriesWrapper >::iterationFilename( uint64_t i )
{
    std::stringstream iteration( "" );
    iteration << 
        std::setw( *get().m_filenamePadding ) << std::setfill( '0' ) << i;
    return *get().m_filenamePrefix + iteration.str() + *get().m_filenamePostfix;
}

template< typename SeriesWrapper >
typename SeriesImpl< SeriesWrapper >::iterations_iterator
SeriesImpl< SeriesWrapper >::indexOf( Iteration const & iteration )
{
    for( 
        auto it = get().iterations.begin();
        it != get().iterations.end();
        ++it )
    {
        if( it->second.m_writable.get() == iteration.m_writable.get() )
        {
            return it;
        }
    }
    throw std::runtime_error(
        "[Iteration::close] Iteration not found in Series." );
}

template< typename SeriesWrapper >
AdvanceStatus
SeriesImpl< SeriesWrapper >::advance(
    AdvanceMode mode,
    internal::AttributableData & file,
    iterations_iterator begin,
    Iteration & iteration )
{
    internal::AttributableData & attri = attributable_t::get();

    auto end = begin;
    ++end;
    /*
     * @todo By calling flushFileBased/GroupBased, we do not propagate tasks to
     *       the backend yet. We will append ADVANCE and CLOSE_FILE tasks
     *       manually. In order to avoid having them automatically appended by
     *       the flush*Based methods, set CloseStatus to Open for now.
     */
    Iteration::CloseStatus oldCloseStatus = *iteration.m_closed;
    if( oldCloseStatus == Iteration::CloseStatus::ClosedInFrontend )
    {
        *iteration.m_closed = Iteration::CloseStatus::Open;
    }

    switch( *get().m_iterationEncoding )
    {
        using IE = IterationEncoding;
        case IE::groupBased:
            flushGroupBased( begin, end );
            break;
        case IE::fileBased:
            flushFileBased( begin, end );
            break;
    }
    *iteration.m_closed = oldCloseStatus;

    Parameter< Operation::ADVANCE > param;
    param.mode = mode;
    IOTask task( &file, param );
    attri.IOHandler->enqueue( task );


    if( oldCloseStatus == Iteration::CloseStatus::ClosedInFrontend &&
        mode == AdvanceMode::ENDSTEP )
    {
        using IE = IterationEncoding;
        switch( *get().m_iterationEncoding )
        {
            case IE::fileBased:
            {
                if( *iteration.m_closed !=
                    Iteration::CloseStatus::ClosedTemporarily )
                {
                    Parameter< Operation::CLOSE_FILE > fClose;
                    attri.IOHandler->enqueue(
                        IOTask( &iteration, std::move( fClose ) ) );
                }
                *iteration.m_closed = Iteration::CloseStatus::ClosedInBackend;
                break;
            }
            case IE::groupBased:
            {
                // We can now put some groups to rest
                Parameter< Operation::CLOSE_PATH > fClose;
                attri.IOHandler->enqueue( 
                    IOTask( &iteration, std::move( fClose ) ) );
                // In group-based iteration layout, files are
                // not closed on a per-iteration basis
                // We will treat it as such nonetheless
                *iteration.m_closed = Iteration::CloseStatus::ClosedInBackend;
            }
            break;
        }
    }

    // We cannot call Series::flush now, since the IO handler is still filled
    // from calling flush(Group|File)based, but has not been emptied yet
    // Do that manually
    attri.IOHandler->flush();

    return *param.status;
}

template< typename SeriesWrapper >
void
SeriesImpl< SeriesWrapper >::openIteration(
    uint64_t index, Iteration iteration )
{
    internal::AttributableData & attri = attributable_t::get();
    SeriesData & series = get();
    Writable * writable = attri.m_writable.get();

    // open the iteration's file again
    Parameter< Operation::OPEN_FILE > fOpen;
    fOpen.name = iterationFilename( index );
    attri.IOHandler->enqueue( IOTask( writable, fOpen ) );

    /* open base path */
    Parameter< Operation::OPEN_PATH > pOpen;
    pOpen.path = auxiliary::replace_first( basePath(), "%T/", "" );
    attri.IOHandler->enqueue( IOTask( &series.iterations, pOpen ) );
    /* open iteration path */
    pOpen.path = std::to_string( index );
    attri.IOHandler->enqueue( IOTask( &iteration, pOpen ) );
    switch( *iteration.m_closed )
    {
        using CL = Iteration::CloseStatus;
        case CL::ClosedInBackend:
            throw std::runtime_error(
                "[Series] Detected illegal access to iteration that "
                "has been closed previously." );
        case CL::Open:
        case CL::ClosedTemporarily:
            *iteration.m_closed = CL::Open;
            break;
        case CL::ClosedInFrontend:
            // just keep it like it is
            break;
        default:
            throw std::runtime_error( "Unreachable!" );
    }
}

namespace
{
    std::string
    cleanFilename(std::string const &filename, Format f) {
        switch (f) {
            case Format::HDF5:
            case Format::ADIOS1:
            case Format::ADIOS2:
            case Format::ADIOS2_SST:
            case Format::JSON:
                return auxiliary::replace_last(filename, suffix(f), "");
            default:
                return filename;
        }
    }

    std::function<std::tuple<bool, int>(std::string const &)>
    buildMatcher(std::string const &regexPattern) {
        std::regex pattern(regexPattern);

        return [pattern](std::string const &filename) -> std::tuple<bool, int> {
            std::smatch regexMatches;
            bool match = std::regex_match(filename, regexMatches, pattern);
            int padding = match ? regexMatches[1].length() : 0;
            return std::tuple<bool, int>{match, padding};
        };
    }

    std::function<std::tuple<bool, int>(std::string const &)>
    matcher(std::string const &prefix, int padding, std::string const &postfix, Format f) {
        switch (f) {
            case Format::HDF5: {
                std::string nameReg = "^" + prefix + "([[:digit:]]";
                if (padding != 0)
                    nameReg += "{" + std::to_string(padding) + "}";
                else
                    nameReg += "+";
                nameReg += +")" + postfix + ".h5$";
                return buildMatcher(nameReg);
            }
            case Format::ADIOS1:
            case Format::ADIOS2: {
                std::string nameReg = "^" + prefix + "([[:digit:]]";
                if (padding != 0)
                    nameReg += "{" + std::to_string(padding) + "}";
                else
                    nameReg += "+";
                nameReg += +")" + postfix + ".bp$";
                return buildMatcher(nameReg);
            }
            case Format::ADIOS2_SST:
            {
                std::string nameReg = "^" + prefix + "([[:digit:]]";
                if( padding != 0 )
                    nameReg += "{" + std::to_string(padding) + "}";
                else
                    nameReg += "+";
                nameReg += + ")" + postfix + ".sst$";
                return buildMatcher(nameReg);
            }
            case Format::JSON: {
                std::string nameReg = "^" + prefix + "([[:digit:]]";
                if (padding != 0)
                    nameReg += "{" + std::to_string(padding) + "}";
                else
                    nameReg += "+";
                nameReg += +")" + postfix + ".json$";
                return buildMatcher(nameReg);
            }
            default:
                return [](std::string const &) -> std::tuple<bool, int> { return std::tuple<bool, int>{false, 0}; };
        }
    }
} // namespace [anonymous]
} // namespace openPMD