#include "fs/nor/NorFileSystem.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

namespace
{
    constexpr fs::NorFeatures minimal{ .makeDirectory = false, .remove = false, .rename = false, .truncate = false, .format = false, .overwrite = false };
}

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::NorFileSystem::WithStorage<validation::footprint::norConfig, 1, minimal>> fileSystem;
    fileSystem.Construct(platform.flash);

    validation::footprint::Exercise(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
