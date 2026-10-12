#ifndef FS_FAT_FAT_FORMAT_LAYOUT_HPP
#define FS_FAT_FAT_FORMAT_LAYOUT_HPP

#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatFormatConfig
    {
        std::optional<uint8_t> sectorsPerCluster;
        std::optional<uint32_t> alignmentSectors;
        uint8_t fatCopies{ 2 };
        uint16_t rootEntries{ 512 };
        bool partitioned{ true };
        uint32_t volumeId{ 0 };
    };

    struct SdFormatParameters
    {
        FatType type{ FatType::fat12 };
        uint8_t sectorsPerCluster{ 0 };
        uint32_t boundaryUnitSectors{ 0 };

        bool operator==(const SdFormatParameters& other) const = default;
    };

    struct FatFormatLayout
    {
        FatType type{ FatType::fat12 };
        std::optional<PartitionEntry> partition;
        BiosParameterBlock parameters;
        uint32_t volumeStart{ 0 };
        uint32_t dataStart{ 0 };
        uint32_t clusterCount{ 0 };

        bool operator==(const FatFormatLayout& other) const = default;
    };

    SdFormatParameters SdParametersFor(uint32_t sectors);
    std::optional<FatFormatLayout> ComputeFormatLayout(uint32_t blocks, uint32_t blockSize, const FatFormatConfig& config);
}

#endif
