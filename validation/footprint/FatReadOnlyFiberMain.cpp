#include "fs/fat/fiber/FatFiberReader.hpp"
#include "infra/util/StaticStorage.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<fs::FatFiberReader::WithStorage<1>> fileSystem;
    fileSystem.Construct(platform.blockDevice);

    validation::footprint::ExerciseReads(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
