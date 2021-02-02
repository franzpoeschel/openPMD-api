#pragma once


#include "openPMD/SeriesInternal.hpp"
#include "openPMD/config.hpp"

#include <memory>

#if openPMD_HAVE_MPI
#    include <mpi.h>
#endif

namespace openPMD
{
class SeriesImpl : public AttributableImpl
{
friend class Iteration;

public:
    SeriesImpl( internal::SeriesData *, internal::AttributableData * );

    std::string
    openPMD() const;
    /** Set the version of the enforced <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#hierarchy-of-the-data-file">openPMD
     * standard</A>.
     *
     * @param   openPMD   String <CODE>MAJOR.MINOR.REVISION</CODE> of the
     * desired version of the openPMD standard.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setOpenPMD( std::string const & openPMD );

    /**
     * @return  32-bit mask of applied extensions to the <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#hierarchy-of-the-data-file">openPMD
     * standard</A>.
     */
    uint32_t
    openPMDextension() const;
    /** Set a 32-bit mask of applied extensions to the <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#hierarchy-of-the-data-file">openPMD
     * standard</A>.
     *
     * @param   openPMDextension  Unsigned 32-bit integer used as a bit-mask of
     * applied extensions.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setOpenPMDextension( uint32_t openPMDextension );

    /**
     * @return  String representing the common prefix for all data sets and
     * sub-groups of a specific iteration.
     */
    std::string
    basePath() const;
    /** Set the common prefix for all data sets and sub-groups of a specific
     * iteration.
     *
     * @param   basePath    String of the common prefix for all data sets and
     * sub-groups of a specific iteration.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setBasePath( std::string const & basePath );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String representing the path to mesh records, relative(!) to
     * <CODE>basePath</CODE>.
     */
    std::string
    meshesPath() const;
    /** Set the path to <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#mesh-based-records">mesh
     * records</A>, relative(!) to <CODE>basePath</CODE>.
     *
     * @param   meshesPath  String of the path to <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#mesh-based-records">mesh
     * records</A>, relative(!) to <CODE>basePath</CODE>.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setMeshesPath( std::string const & meshesPath );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String representing the path to particle species, relative(!) to
     * <CODE>basePath</CODE>.
     */
    std::string
    particlesPath() const;
    /** Set the path to groups for each <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#particle-records">particle
     * species</A>, relative(!) to <CODE>basePath</CODE>.
     *
     * @param   particlesPath   String of the path to groups for each <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#particle-records">particle
     * species</A>, relative(!) to <CODE>basePath</CODE>.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setParticlesPath( std::string const & particlesPath );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String indicating author and contact for the information in the
     * file.
     */
    std::string
    author() const;
    /** Indicate the author and contact for the information in the file.
     *
     * @param   author  String indicating author and contact for the information
     * in the file.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setAuthor( std::string const & author );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String indicating the software/code/simulation that created the
     * file;
     */
    std::string
    software() const;
    /** Indicate the software/code/simulation that created the file.
     *
     * @param   newName    String indicating the software/code/simulation that
     * created the file.
     * @param   newVersion String indicating the version of the
     * software/code/simulation that created the file.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setSoftware(
        std::string const & newName,
        std::string const & newVersion = std::string( "unspecified" ) );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String indicating the version of the software/code/simulation
     * that created the file.
     */
    std::string
    softwareVersion() const;
    /** Indicate the version of the software/code/simulation that created the
     * file.
     *
     * @deprecated Set the version with the second argument of setSoftware()
     *
     * @param   softwareVersion String indicating the version of the
     * software/code/simulation that created the file.
     * @return  Reference to modified series.
     */
    [[deprecated( "Set the version with the second argument of "
                  "setSoftware()" )]] SeriesImpl &
    setSoftwareVersion( std::string const & softwareVersion );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String indicating date of creation.
     */
    std::string
    date() const;
    /** Indicate the date of creation.
     *
     * @param   date    String indicating the date of creation.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setDate( std::string const & date );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String indicating dependencies of software that were used to
     * create the file.
     */
    std::string
    softwareDependencies() const;
    /** Indicate dependencies of software that were used to create the file.
     *
     * @param   newSoftwareDependencies String indicating dependencies of
     * software that were used to create the file (semicolon-separated list if
     * needed).
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setSoftwareDependencies( std::string const & newSoftwareDependencies );

    /**
     * @throw   no_such_attribute_error If optional attribute is not present.
     * @return  String indicating the machine or relevant hardware that created
     * the file.
     */
    std::string
    machine() const;
    /** Indicate the machine or relevant hardware that created the file.
     *
     * @param   newMachine String indicating the machine or relevant hardware
     * that created the file (semicolon-separated list if needed)..
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setMachine( std::string const & newMachine );

    /**
     * @return  Current encoding style for multiple iterations in this series.
     */
    IterationEncoding
    iterationEncoding() const;
    /** Set the <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#iterations-and-time-series">encoding
     * style</A> for multiple iterations in this series.
     *
     * @param   iterationEncoding   Desired <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#iterations-and-time-series">encoding
     * style</A> for multiple iterations in this series.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setIterationEncoding( IterationEncoding iterationEncoding );

    /**
     * @return  String describing a <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#iterations-and-time-series">pattern</A>
     * describing how to access single iterations in the raw file.
     */
    std::string
    iterationFormat() const;
    /** Set a <A
     * HREF="https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#iterations-and-time-series">pattern</A>
     * describing how to access single iterations in the raw file.
     *
     * @param   iterationFormat String with the iteration regex <CODE>\%T</CODE>
     * defining either the series of files (fileBased) or the series of groups
     * within a single file (groupBased) that allows to extract the iteration
     * from it. For fileBased formats the iteration must be included in the file
     * name. The format depends on the selected iterationEncoding method.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setIterationFormat( std::string const & iterationFormat );

    /**
     * @return String of a pattern for file names.
     */
    std::string
    name() const;

    /** Set the pattern for file names.
     *
     * @param   name    String of the pattern for file names. Must include
     * iteration regex <CODE>\%T</CODE> for fileBased data.
     * @return  Reference to modified series.
     */
    SeriesImpl &
    setName( std::string const & name );

    /** The currently used backend
     *
     * @see AbstractIOHandler::backendName()
     *
     * @return String of a pattern for data backend.
     */
    std::string
    backend() const;

    /** Execute all required remaining IO operations to write or read data.
     */
    void flush();

protected:
    using iterations_t = decltype(internal::SeriesData::iterations);
    using iterations_iterator = iterations_t::iterator;

    struct ParsedInput;

    internal::SeriesData * m_series;

    inline internal::SeriesData &
    get()
    {
        return *m_series;
    }

    inline internal::SeriesData const &
    get() const
    {
        return *m_series;
    }

    std::unique_ptr< ParsedInput > parseInput(std::string);
    void init(std::shared_ptr< AbstractIOHandler >, std::unique_ptr< ParsedInput >);
    void initDefaults();
    void flushFileBased( iterations_iterator begin, iterations_iterator end );
    void flushGroupBased( iterations_iterator begin, iterations_iterator end );
    void flushMeshesPath();
    void flushParticlesPath();
    void readFileBased( );
    std::future<void> flush_impl(iterations_iterator begin, iterations_iterator end);

    /**
     * Note on re-parsing of a Series:
     * If init == false, the parsing process will seek for new
     * Iterations/Records/Record Components etc.
     * Re-parsing of objects that have already been parsed is not implemented
     * as of yet. Such a facility will be required upon implementing things such
     * as resizable datasets.
     */
    void readGroupBased( bool init = true );
    void readBase();
    void read();
    std::string iterationFilename( uint64_t i );
    void openIteration( uint64_t index, Iteration iteration );

    /**
     * Find the given iteration in Series::iterations and return an iterator
     * into Series::iterations at that place.
     */
    iterations_iterator
    indexOf( Iteration const & );

    /**
     * @brief In step-based IO mode, begin or end an IO step for the given
     *        iteration.
     *
     * Called internally by Iteration::beginStep and Iteration::endStep.
     *
     * @param mode Whether to begin or end a step.
     * @param file The Attributable representing the iteration. In file-based
     *             iteration layout, this is an Iteration object, in group-
     *             based layout, it's the Series object.
     * @param it The iterator within Series::iterations pointing to that
     *           iteration.
     * @param iteration The actual Iteration object.
     * @return AdvanceStatus
     */
    AdvanceStatus
    advance(
        AdvanceMode mode,
        internal::AttributableData & file,
        iterations_iterator it,
        Iteration & iteration );
};

namespace internal
{
    class SeriesInternal
        : public SeriesData
        , public SeriesImpl
    {
        friend struct SeriesShared;

    public:
#if openPMD_HAVE_MPI
        SeriesInternal(
            std::string const & filepath,
            Access at,
            MPI_Comm comm,
            std::string const & options = "{}" );
#endif

        SeriesInternal(
            std::string const & filepath,
            Access at,
            std::string const & options = "{}" );
        // @todo make AttributableImpl<>::linkHierarchy non-virtual
        virtual ~SeriesInternal();

        inline
        SeriesData & getSeries()
        {
            return *this;
        }

        inline
        SeriesData const & getSeries() const
        {
            return *this;
        }

        inline
        AttributableData & getAttributable()
        {
            return *this;
        }

        inline
        AttributableData const & getAttributable() const
        {
            return *this;
        }
    };
} // namespace internal

class ReadIterations;
class WriteIterations;

class Series : public SeriesImpl
{
private:
    std::shared_ptr< internal::SeriesInternal > m_series;

public:
#if openPMD_HAVE_MPI
    Series(
        std::string const & filepath,
        Access at,
        MPI_Comm comm,
        std::string const & options = "{}" );
#endif

    Series(
        std::string const & filepath,
        Access at,
        std::string const & options = "{}" );
    
    virtual ~Series() = default;

    Container< Iteration, uint64_t > iterations;

    /**
     * @brief Entry point to the reading end of the streaming API.
     *
     * Creates and returns an instance of the ReadIterations class which can
     * be used for iterating over the openPMD iterations in a C++11-style for
     * loop.
     * Look for the ReadIterations class for further documentation.
     *
     * @return ReadIterations
     */
    ReadIterations readIterations();

    /**
     * @brief Entry point to the writing end of the streaming API.
     *
     * Creates and returns an instance of the WriteIterations class which is a
     * restricted container of iterations which takes care of
     * streaming semantics.
     * The created object is stored as member of the Series object, hence this
     * method may be called as many times as a user wishes.
     * Look for the WriteIterations class for further documentation.
     *
     * @return WriteIterations
     */
    WriteIterations writeIterations();

    // @todo make these private
    inline
    internal::SeriesData & getSeries()
    {
        return m_series->getSeries();
    }

    inline
    internal::SeriesData const & getSeries() const
    {
        return m_series->getSeries();
    }

    inline
    internal::AttributableData & getAttributable()
    {
        return m_series->getAttributable();
    }

    inline
    internal::AttributableData const & getAttributable() const
    {
        return m_series->getAttributable();
    }
};


/**
 * @brief Subclass of Iteration that knows its own index withing the containing
 *        Series.
 */
class IndexedIteration : public Iteration
{
    friend class SeriesIterator;

public:
    using iterations_t = decltype(internal::SeriesData::iterations);
    using index_t = iterations_t::key_type;
    index_t const iterationIndex;

private:
    template<typename Iteration_t>
    IndexedIteration(Iteration_t&& it, index_t index) : Iteration(std::forward<Iteration_t>(it))
                                                      , iterationIndex(index)
    {
    }
};

class SeriesIterator
{
    using iteration_index_t = IndexedIteration::index_t;

    using maybe_series_t = auxiliary::Option<Series>;

    maybe_series_t m_series;
    iteration_index_t m_currentIteration = 0;

    //! construct the end() iterator
    SeriesIterator();

public:
    SeriesIterator(Series);

    SeriesIterator& operator++();

    IndexedIteration operator*();

    bool operator==(SeriesIterator const& other) const;

    bool operator!=(SeriesIterator const& other) const;

    static SeriesIterator end();
};

/**
 * @brief Reading side of the streaming API.
 *
 * Create instance via Series::readIterations().
 * For use in a C++11-style foreach loop over iterations.
 * Designed to allow reading any kind of Series, streaming and non-
 * streaming alike.
 * Calling Iteration::close() manually before opening the next iteration is
 * encouraged and will implicitly flush all deferred IO actions.
 * Otherwise, Iteration::close() will be implicitly called upon
 * SeriesIterator::operator++(), i.e. upon going to the next iteration in
 * the foreach loop.
 * Since this is designed for streaming mode, reopening an iteration is
 * not possible once it has been closed.
 *
 */
class ReadIterations
{
    friend class Series;

private:
    using iterations_t = decltype(internal::SeriesData::iterations);
    using iterator_t = SeriesIterator;

    Series m_series;

    ReadIterations(Series);

public:
    iterator_t begin();

    iterator_t end();
};

// @todo needed to move WriteIterations to SeriesInternal.hpp temporarily
} // namespace openPMD
