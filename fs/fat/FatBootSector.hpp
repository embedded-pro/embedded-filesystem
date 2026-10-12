#ifndef FS_FAT_FAT_BOOT_SECTOR_HPP
#define FS_FAT_FAT_BOOT_SECTOR_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    enum class FatType : uint8_t
    {
        fat12,
        fat16,
        fat32
    };

    struct BiosParameterBlock
    {
        uint16_t bytesPerSector{ 0 };
        uint8_t sectorsPerCluster{ 0 };
        uint16_t reservedSectors{ 0 };
        uint8_t fatCopies{ 0 };
        uint16_t rootEntries{ 0 };
        uint32_t totalSectors{ 0 };
        uint8_t media{ 0 };
        uint32_t sectorsPerFat{ 0 };
        uint32_t hiddenSectors{ 0 };
        bool fat32Layout{ false };
        uint16_t extendedFlags{ 0 };
        uint16_t fileSystemVersion{ 0 };
        uint32_t rootCluster{ 0 };
        uint16_t fsInfoSector{ 0 };
        uint16_t backupBootSector{ 0 };
        uint32_t volumeId{ 0 };

        bool operator==(const BiosParameterBlock& other) const = default;
    };

    std::optional<BiosParameterBlock> ParseBootSector(infra::ConstByteRange sector);
    void SerializeBootSector(const BiosParameterBlock& parameters, FatType type, infra::ByteRange sector);
}

#endif
