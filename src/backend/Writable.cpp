/* Copyright 2017-2019 Fabian Koller
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
#include <openPMD/IO/AbstractIOHandler.hpp>
#include <openPMD/backend/Writable.hpp>
#include "openPMD/IO/IOTask.hpp"


namespace openPMD
{
Writable::Writable(Attributable* a)
        : abstractFilePosition{nullptr},
          IOHandler{nullptr},
          attributable{a},
          parent{nullptr},
          dirty{true},
          written{false}
{ }

Writable::~Writable() = default;

auxiliary::ConsumingFuture< AdvanceStatus >
Writable::advance( AdvanceMode mode )
{
    Parameter< Operation::ADVANCE > param;
    param.mode = mode;
    IOTask task( this, param );
    IOHandler->enqueue( task );
    // this flush will
    // (1) flush all actions that are still queued up
    // (2) finally run the advance task

    auto first_future = IOHandler->flush();
    auto param_ptr = std::make_shared< Parameter< Operation::ADVANCE > >(
        std::move( param ) );
    std::packaged_task< AdvanceStatus() > ptask( [param_ptr]() {
        if( param_ptr->task )
        {
            auto future = param_ptr->task->get_future();
            future.wait();
            return future.get();
        }
        else
        {
            throw std::runtime_error(
                "Internal error (Writable::advance()):"
                " backend has not properly finished the ADVANCE task." );
        }
    } );

    return auxiliary::chain_futures< void, AdvanceStatus >(
        std::move( first_future ), std::move( ptask ) );
}

} // openPMD
