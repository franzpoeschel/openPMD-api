/* Copyright 2017-2020 Fabian Koller, Axel Huebl
 *
 * This file is part of openPMD-api.
 *
 * openPMD-api is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * openPMD-api is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with openPMD-api.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "openPMD/config.hpp"
#include "openPMD/backend/Attributable.hpp"
#include "openPMD/backend/Container.hpp"
#include "openPMD/IO/AbstractIOHandler.hpp"
#include "openPMD/IO/Access.hpp"
#include "openPMD/IO/Format.hpp"
#include "openPMD/Iteration.hpp"
#include "openPMD/IterationEncoding.hpp"
#include "openPMD/Streaming.hpp"
#include "openPMD/auxiliary/Option.hpp"
#include "openPMD/auxiliary/Variant.hpp"
#include "openPMD/backend/Attributable.hpp"
#include "openPMD/backend/Container.hpp"
#include "openPMD/config.hpp"
#include "openPMD/version.hpp"

#if openPMD_HAVE_MPI
#   include <mpi.h>
#endif

#include <map>
#include <string>

// expose private and protected members for invasive testing
#ifndef OPENPMD_private
#   define OPENPMD_private private
#endif


namespace openPMD
{
class WriteIterations;
class Series;
class SeriesImpl;

namespace internal
{
/** @brief  Root level of the openPMD hierarchy.
 *
 * Entry point and common link between all iterations of particle and mesh data.
 *
 * @see https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#hierarchy-of-the-data-file
 * @see https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#iterations-and-time-series
 */
class SeriesData : public AttributableData
{
friend class openPMD::SeriesImpl;
friend class openPMD::Iteration;
friend class openPMD::Series;

public:
    explicit SeriesData() = default;

    SeriesData(SeriesData const &) = delete;
    SeriesData(SeriesData &&) = delete;

    SeriesData & operator=(SeriesData const &) = delete;
    SeriesData & operator=(SeriesData &&) = delete;

    virtual ~SeriesData() = default;

    Container< Iteration, uint64_t > iterations;

OPENPMD_private:
    /**
     *  Whether a step is currently active for this iteration.
     * Used for group-based iteration layout, see SeriesData.hpp for
     * iteration-based layout.
     * Access via stepStatus() method to automatically select the correct
     * one among both flags.
     */
    std::shared_ptr< StepStatus > m_stepStatus =
        std::make_shared< StepStatus >( StepStatus::NoStep );

    static constexpr char const * const BASEPATH = "/data/%T/";

    std::shared_ptr< IterationEncoding > m_iterationEncoding{
        std::make_shared< IterationEncoding >()
    };
    std::shared_ptr< std::string > m_name;
    std::shared_ptr< Format > m_format;

    std::shared_ptr< std::string > m_filenamePrefix;
    std::shared_ptr< std::string > m_filenamePostfix;
    std::shared_ptr< int > m_filenamePadding;

    std::shared_ptr< auxiliary::Option< WriteIterations > > m_writeIterations =
        std::make_shared< auxiliary::Option< WriteIterations > >();
}; // SeriesData
} // namespace internal



/** Writing side of the streaming API.
 *
 * Create instance via Series::writeIterations().
 * For use via WriteIterations::operator[]().
 * Designed to allow reading any kind of Series, streaming and non-
 * streaming alike. Calling Iteration::close() manually before opening
 * the next iteration is encouraged and will implicitly flush all
 * deferred IO actions. Otherwise, Iteration::close() will be implicitly
 * called upon SeriesIterator::operator++(), i.e. upon going to the next
 * iteration in the foreach loop.
 *
 * Since this is designed for streaming mode, reopening an iteration is
 * not possible once it has been closed.
 *
 */
class WriteIterations : private Container< Iteration, uint64_t >
{
    friend class Series;

private:
    using iterations_t = Container< Iteration, uint64_t >;
    struct SharedResources
    {
        iterations_t iterations;
        auxiliary::Option< uint64_t > currentlyOpen;

        SharedResources( iterations_t );
        ~SharedResources();
    };

    using key_type = typename iterations_t::key_type;
    using value_type = typename iterations_t::key_type;
    WriteIterations( iterations_t );
    explicit WriteIterations() = default;
    //! Index of the last opened iteration
    std::shared_ptr< SharedResources > shared;

public:
    mapped_type &
    operator[]( key_type const & key ) override;
    mapped_type &
    operator[]( key_type && key ) override;
};
} // namespace openPMD
