#include "fs/fat/lite/FatLite.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::FatLite::WithStorage<1, 1, fs::PathLimits{}, fs::FatReaderFeatures{ .longNames = false }>> fileSystem;
    fileSystem.Construct(platform.blockDevice);

    validation::footprint::Exercise(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
