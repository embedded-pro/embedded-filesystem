#include "infra/util/StaticStorage.hpp"
#include "spike/fat_reader/FatReader.hpp"
#include "validation/footprint/Exercise.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<spike::FatReader::WithStorage<1, 1>> fileSystem;
    fileSystem.Construct(platform.blockDevice);

    validation::footprint::ExerciseReads(*fileSystem, infra::MakeRange(platform.buffer));

    platform.Run();
}
