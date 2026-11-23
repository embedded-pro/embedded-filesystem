#pragma once

#include "fs/interfaces/FileSystem.hpp"
#include "infra/util/ByteRange.hpp"

namespace validation::footprint
{
    void Exercise(fs::FileSystem& fileSystem, infra::ByteRange buffer);
    void ExerciseReads(fs::FileSystem& fileSystem, infra::ByteRange buffer);
}
