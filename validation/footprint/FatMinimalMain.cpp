#include "fs/fat/FatFileSystem.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

namespace
{
    constexpr fs::FatFeatures minimal{ .makeDirectory = false, .remove = false, .rename = false, .expectSize = false, .usage = false, .backgroundScan = false, .timestamps = false, .cleanShutdown = false, .longNames = false };
}

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::FatFileSystem::WithStorage<1, 8, fs::FatLimits{}, minimal>> fileSystem;
    fileSystem.Construct(platform.blockDevice, fs::FatConfig{ .blocksPerAllocationUnit = validation::footprint::allocationUnitBlocks });

    validation::footprint::Exercise(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
