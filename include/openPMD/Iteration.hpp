/* Copyright 2017-2020 Fabian Koller
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

#include "openPMD/backend/Attributable.hpp"
#include "openPMD/backend/Container.hpp"
#include "openPMD/Mesh.hpp"
#include "openPMD/ParticleSpecies.hpp"


namespace openPMD
{
/** @brief  Logical compilation of data from one snapshot (e.g. a single simulation cycle).
 *
 * @see https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#required-attributes-for-the-basepath
 */
class Iteration : public Attributable
{
    template<
            typename T,
            typename T_key,
            typename T_container
    >
    friend class Container;
    friend class Series;

public:
    Iteration(Iteration const&);
    Iteration& operator=(Iteration const&);

    /**
     * @tparam  T   Floating point type of user-selected precision (e.g. float, double).
     * @return  Global reference time for this iteration.
     */
    template< typename T >
    T time() const;
    /** Set the global reference time for this iteration.
     *
     * @tparam  T       Floating point type of user-selected precision (e.g. float, double).
     * @param   newTime Global reference time for this iteration.
     * @return  Reference to modified iteration.
     */
    template< typename T >
    Iteration& setTime(T newTime);

    /**
     * @tparam  T   Floating point type of user-selected precision (e.g. float, double).
     * @return  Time step used to reach this iteration.
     */
    template< typename T >
    T dt() const;
    /** Set the time step used to reach this iteration.
     *
     * @tparam  T     Floating point type of user-selected precision (e.g. float, double).
     * @param   newDt Time step used to reach this iteration.
     * @return  Reference to modified iteration.
     */
    template< typename T >
    Iteration& setDt(T newDt);

    /**
     * @return Conversion factor to convert time and dt to seconds.
     */
    double timeUnitSI() const;
    /** Set the conversion factor to convert time and dt to seconds.
     *
     * @param  newTimeUnitSI new value for timeUnitSI
     * @return Reference to modified iteration.
     */
    Iteration& setTimeUnitSI(double newTimeUnitSI);

    /**
     * @brief Declare an iteration finalized. Indicates that no further
     * alterations will be made to this iteration.
     * @return Reference to modified iteration.
     */
    Iteration &
    close( bool flush = true );

    bool
    closed() const;

    bool
    closedByWriter() const;

    Container< Mesh > meshes;
    Container< ParticleSpecies > particles; //particleSpecies?

    virtual ~Iteration() = default;
private:
    Iteration();

    void flushFileBased(std::string const&, uint64_t);
    void flushGroupBased(uint64_t);
    void flush();
    void read();

    enum class CloseStatus
    {
        Open,
        ClosedInFrontend,
        ClosedInBackend
    };

    /*
     * An iteration may be logically closed in the frontend,
     * but not necessarily yet in the backend.
     * Will be propagated to the backend upon next flush.
     * Store the current status.
     * Once an iteration has been closed, no further flushes shall be performed.
     * If flushing a closed file, the old file may otherwise be overwritten.
     */
    std::shared_ptr< CloseStatus > m_closed =
        std::make_shared< CloseStatus >( CloseStatus::Open );

    /**
     * @brief Verify that a closed iteration has not been wrongly accessed.
     *
     * @return true If closed iteration had no wrong accesses.
     * @return false Otherwise.
     */
    bool
    verifyClosed() const;

    virtual void linkHierarchy(std::shared_ptr< Writable > const& w);
};  // Iteration

extern template
float
Iteration::time< float >() const;

extern template
double
Iteration::time< double >() const;

extern template
long double
Iteration::time< long double >() const;

template< typename T >
inline T
Iteration::time() const
{ return Attributable::readFloatingpoint< T >("time"); }


extern template
float
Iteration::dt< float >() const;

extern template
double
Iteration::dt< double >() const;

extern template
long double
Iteration::dt< long double >() const;

template< typename T >
inline T
Iteration::dt() const
{ return Attributable::readFloatingpoint< T >("dt"); }
} // openPMD
