#include <openPMD/openPMD.hpp>

#include <chrono>
#include <iostream>
#include <numeric>

int main()
{
    using field_dt = uint32_t;
    openPMD::Offset imageOffset{0, 0};
    openPMD::Extent imageExtent{10240, 1024};
    openPMD::Extent::value_type flattenedExtent = std::accumulate(
        imageExtent.begin(), imageExtent.end(), 1, [](auto left, auto right) {
            return left * right;
        });
    unsigned numImagesPerShot = 10;
    unsigned numIterations = 100;
    std::string filename = "./stream.sst";
    std::string tomlConfig = R"(
[adios2.engine.parameters]
QueueLimit = 2
)";

    double mbytePerShot =
        (flattenedExtent * numImagesPerShot * sizeof(field_dt)) /
        double(1024 * 1024);
    std::cout << "Writing " << mbytePerShot << "MB per iteration." << std::endl;

    std::vector<field_dt> baseData(flattenedExtent);
    std::iota(baseData.begin(), baseData.end(), 0);

    openPMD::Series write(filename, openPMD::Access::CREATE, tomlConfig);
    openPMD::Dataset ds(openPMD::determineDatatype<field_dt>(), imageExtent);

    using Clock = std::chrono::system_clock;
    Clock::time_point previous{};

    std::cout << "Durations between iterations in seconds: \n" << std::endl;

    for (unsigned currentIteration = 0; currentIteration < numIterations;
         ++currentIteration)
    {
        auto iteration = write.writeIterations()[currentIteration];
        for (unsigned imageCount = 0; imageCount < numImagesPerShot;
             ++imageCount)
        {
            auto image = iteration.meshes["image_" + std::to_string(imageCount)]
                                         [openPMD::RecordComponent::SCALAR];
            image.resetDataset(ds);
            image.storeChunk(baseData, imageOffset, imageExtent);
        }
        iteration.close();
        Clock::time_point now = Clock::now();
        if (currentIteration > 0)
        {
            auto diffInNanoseconds = (now - previous).count();
            auto seconds = diffInNanoseconds / 1000000000;
            auto remainder = diffInNanoseconds % 1000000000;
            std::cout << seconds << "." << remainder << "s" << std::endl;
        }
        previous = now;
    }
}
