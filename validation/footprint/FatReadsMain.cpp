#include "fs/fat/FatFileSystem.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::FatFileSystem::WithStorage<1, 8>> fileSystem;
    fileSystem.Construct(platform.blockDevice, fs::FatConfig{ .blocksPerAllocationUnit = validation::footprint::allocationUnitBlocks });

    validation::footprint::ExerciseReads(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
