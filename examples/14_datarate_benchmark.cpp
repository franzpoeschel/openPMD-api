#include <openPMD/openPMD.hpp>

#include <numeric>

int main()
{
    using field_dt = uint32_t;
    openPMD::Offset imageOffset{0,0};
    openPMD::Extent imageExtent{1024, 1024};
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

    std::vector<field_dt> baseData(flattenedExtent);
    std::iota(baseData.begin(), baseData.end(), 0);

    openPMD::Series write(filename, openPMD::Access::CREATE, tomlConfig);
    openPMD::Dataset ds(openPMD::determineDatatype<field_dt>(), imageExtent);

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
    }
}
