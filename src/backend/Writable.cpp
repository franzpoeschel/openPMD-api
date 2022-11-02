/* Copyright 2017-2021 Fabian Koller
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
#include "openPMD/backend/Writable.hpp"
#include "openPMD/Series.hpp"
#include "openPMD/auxiliary/DerefDynamicCast.hpp"
#include "openPMD/auxiliary/Variant.hpp"

namespace openPMD
{
Writable::Writable(internal::AttributableData *a) : attributable{a}
{}

void Writable::seriesFlush(std::string backendConfig)
{
    seriesFlush({FlushLevel::UserFlush, std::move(backendConfig)});
}

void Writable::seriesFlush(internal::FlushParams flushParams)
{
    auto series =
        Attributable({attributable, [](auto const *) {}}).retrieveSeries();
    series.flush_impl(
        series.iterations.begin(), series.iterations.end(), flushParams);
}

auto Writable::maybeIOHandler() const -> AbstractIOHandler const *
{
    auto handleOptional = [](std::shared_ptr<MaybeIOHandler> const &opt)
        -> AbstractIOHandler const * {
        if (!opt || !opt->has_value())
        {
            return nullptr;
        }
        return &*opt->value();
    };
    return std::visit(
        auxiliary::overloaded{
            [&handleOptional](std::shared_ptr<MaybeIOHandler> const &ptr) {
                return handleOptional(ptr);
            },
            [&handleOptional](std::weak_ptr<MaybeIOHandler> const &ptr) {
                return handleOptional(ptr.lock());
            }},
        IOHandler);
}

auto Writable::maybeIOHandler() -> AbstractIOHandler *
{
    return const_cast<AbstractIOHandler *>(
        static_cast<Writable const *>(this)->maybeIOHandler());
}

auto Writable::weakCopyOfIOHandler() -> std::weak_ptr<MaybeIOHandler>
{
    return std::visit(
        [](auto &ptr) -> std::weak_ptr<MaybeIOHandler> { return ptr; },
        IOHandler);
}
} // namespace openPMD
