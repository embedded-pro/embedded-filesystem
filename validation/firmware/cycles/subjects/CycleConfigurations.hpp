#ifndef VALIDATION_FIRMWARE_CYCLES_SUBJECTS_CYCLE_CONFIGURATIONS_HPP
#define VALIDATION_FIRMWARE_CYCLES_SUBJECTS_CYCLE_CONFIGURATIONS_HPP

#include "fs/fat/FatFileSystem.hpp"
#include "fs/nor/NorFileSystem.hpp"
#include <cstdint>

namespace validation::cycles
{
    constexpr uint32_t ramFlashSectors = 512;
    constexpr uint32_t ramFlashSectorSize = 4096;
    constexpr uint32_t ramBlockDeviceBlocks = 4096;

    constexpr fs::NorConfig norConfig{ .maxBlocks = ramFlashSectors, .programUnit = 1, .pageSize = 256 };

    using NorFileSystem = fs::NorFileSystem::WithStorage<norConfig, 1>;
    using FatFileSystem = fs::FatFileSystem::WithStorage<2, 8>;

    // As on the host, the RAM of a FAT volume is counted with one open file
    constexpr std::size_t fatRamBytes = sizeof(fs::FatFileSystem::WithStorage<1, 8>);
    constexpr std::size_t norRamBytes = sizeof(NorFileSystem);
}

#endif
