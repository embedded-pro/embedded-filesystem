#ifndef VALIDATION_FOOTPRINT_CONFIGURATIONS_HPP
#define VALIDATION_FOOTPRINT_CONFIGURATIONS_HPP

#include "fs/nor/NorConfig.hpp"
#include <cstdint>

namespace validation::footprint
{
    constexpr uint32_t norSectors = 4096;
    constexpr uint32_t norSectorSize = 4096;
    constexpr fs::NorConfig norConfig{ .maxBlocks = 4096, .programUnit = 1, .pageSize = 256 };

    constexpr uint32_t blockDeviceBlocks = 524288;
    constexpr uint32_t allocationUnitBlocks = 8192;

    struct LittleFsConfiguration
    {
        uint32_t readSize;
        uint32_t programSize;
        uint32_t cacheSize;
        uint32_t lookaheadSize;
        int32_t blockCycles;
        bool collectGarbageWhenIdle;
    };

    constexpr LittleFsConfiguration littleFsDefault{ 16, 16, 16, 16, 500, false };
    constexpr LittleFsConfiguration littleFsRamMatched{ 16, 16, norSectorSize, 512, 500, true };
}

#endif
