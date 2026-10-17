#include "fs/nor/lite/NorLite.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::NorLite::WithStorage<validation::footprint::norConfig, 1>> fileSystem;
    fileSystem.Construct(platform.flash);

    validation::footprint::Exercise(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
