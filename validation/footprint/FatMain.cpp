#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::FatFileSystem::WithStorage<1, 8>> fileSystem;
    fileSystem.Construct(platform.blockDevice, fs::FatConfig{ .blocksPerAllocationUnit = validation::footprint::allocationUnitBlocks });
    fs::FatFormatter::WithStorage<8> formatter(platform.blockDevice);

    formatter.Format(fs::FatFormatConfig{ .alignmentSectors = validation::footprint::allocationUnitBlocks }, [](fs::Result)
        {
            validation::footprint::Exercise(*fileSystem, infra::MakeRange(platform.buffer));
        });

    platform.Run();
}
