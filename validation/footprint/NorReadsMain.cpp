#include "fs/nor/NorFileSystem.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::NorFileSystem::WithStorage<validation::footprint::norConfig, 1>> fileSystem;
    fileSystem.Construct(platform.flash);

    validation::footprint::ExerciseReads(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
