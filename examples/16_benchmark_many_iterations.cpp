/* Copyright 2026 openPMD contributors
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
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with openPMD-api.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

/**
 * Benchmark for writing/reading very many, very small Iterations in linear
 * access modes. Demonstrates that the per-Iteration time does not grow as the
 * number of Iterations increases.
 *
 * Usage:
 *   ./16_benchmark_many_iterations <file> <n_iterations> [write|read]
 *       [file|group|variable] [json-config]
 *
 * Examples:
 *   # HDF5, one tiny file per Iteration
 *   ./16_benchmark_many_iterations many_%T.h5 100000 write file
 *   ./16_benchmark_many_iterations many_%T.h5 100000 read file
 *
 *   # ADIOS2 variable-based encoding (single file, one step per Iteration)
 *   ./16_benchmark_many_iterations many.bp5 100000 write variable
 *   ./16_benchmark_many_iterations many.bp5 100000 read variable
 *
 *   # ADIOS2 SST streaming (needs a reader at the same time)
 *   ./16_benchmark_many_iterations many.sst 100000 write variable
 */

#include <openPMD/openPMD.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace openPMD;
using clock_t_ = std::chrono::steady_clock;

namespace
{
long currentRSSkB()
{
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
    {
        if (line.rfind("VmRSS:", 0) == 0)
        {
            return std::stol(line.substr(6));
        }
    }
#endif
    return -1;
}

IterationEncoding parseEncoding(std::string const &s)
{
    if (s == "group" || s == "groupBased")
        return IterationEncoding::groupBased;
    if (s == "variable" || s == "variableBased")
        return IterationEncoding::variableBased;
    return IterationEncoding::fileBased;
}

void report(
    std::string const &tag,
    std::vector<double> const &us,
    std::string const &file)
{
    if (us.empty())
        return;
    double total = std::accumulate(us.begin(), us.end(), 0.0);
    std::cout << "[" << tag << "] iterations=" << us.size()
              << " total=" << total / 1e6 << "s"
              << " mean=" << total / us.size() << "us"
              << " rss=" << currentRSSkB() << "kB\n";

    constexpr size_t buckets = 10;
    size_t perBucket = std::max<size_t>(1, us.size() / buckets);
    for (size_t b = 0; b < buckets; ++b)
    {
        size_t begin = b * perBucket;
        if (begin >= us.size())
        {
            break;
        }
        size_t end = std::min(us.size(), begin + perBucket);
        double bucketTotal =
            std::accumulate(us.begin() + begin, us.begin() + end, 0.0);
        std::cout << "  decile " << b << " [" << begin << "," << end
                  << "): " << bucketTotal / (end - begin) << "us/iteration\n";
    }
    std::cout << "  file: " << file << std::endl;
}

void writeIterations(
    std::string const &file,
    size_t n,
    IterationEncoding encoding,
    std::string const &config)
{
    Series series(file, Access::CREATE_LINEAR, config);
    series.setIterationEncoding(encoding);
    std::vector<int> data{0, 1, 2, 3};
    std::vector<double> timings;
    timings.reserve(n);
    auto snapshots = series.snapshots();
    for (size_t i = 0; i < n; ++i)
    {
        auto start = clock_t_::now();
        auto iteration = snapshots[i];
        auto component = iteration.meshes["E"]["x"];
        component.resetDataset({Datatype::INT, {4}});
        component.storeChunk(data, {0}, {4});
        iteration.close();
        timings.push_back(
            std::chrono::duration<double, std::micro>(clock_t_::now() - start)
                .count());
    }
    series.close();
    report("write", timings, file);
}

void readIterations(
    std::string const &file,
    size_t n,
    IterationEncoding encoding,
    std::string const &config)
{
    Series series(file, Access::READ_LINEAR, config);
    (void)encoding;
    std::vector<double> timings;
    timings.reserve(n);
    size_t counter = 0;
    for (auto &[index, iteration] : series.snapshots())
    {
        (void)index;
        auto start = clock_t_::now();
        auto component = iteration.meshes["E"]["x"];
        auto chunk = component.loadChunk<int>({0}, {4});
        iteration.close();
        chunk.get();
        timings.push_back(
            std::chrono::duration<double, std::micro>(clock_t_::now() - start)
                .count());
        ++counter;
    }
    series.close();
    if (counter != n)
    {
        std::cerr << "[read] expected " << n << " iterations, found " << counter
                  << std::endl;
    }
    report("read", timings, file);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <file> <n_iterations> [write|read|both] "
                     "[file|group|variable] [json-config]\n";
        return 1;
    }
    std::string file = argv[1];
    size_t n = static_cast<size_t>(std::stoull(argv[2]));
    std::string mode = argc > 3 ? argv[3] : "both";
    IterationEncoding encoding =
        argc > 4 ? parseEncoding(argv[4]) : IterationEncoding::fileBased;
    std::string config = argc > 5 ? argv[5] : "{}";

    if (mode == "write" || mode == "both")
    {
        writeIterations(file, n, encoding, config);
    }
    if (mode == "read" || mode == "both")
    {
        readIterations(file, n, encoding, config);
    }
    return 0;
}
