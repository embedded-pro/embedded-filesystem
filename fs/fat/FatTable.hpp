#ifndef FS_FAT_FAT_TABLE_HPP
#define FS_FAT_FAT_TABLE_HPP

#include "fs/fat/FatBootSector.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatEntryLocation
    {
        uint32_t sector{ 0 };
        uint32_t offset{ 0 };
        bool straddles{ false };

        bool operator==(const FatEntryLocation& other) const = default;
    };

    FatEntryLocation LocateEntry(FatType type, uint32_t cluster, uint32_t sectorSize);
    uint32_t ReadEntry(FatType type, uint32_t cluster, infra::ConstByteRange sector, infra::ConstByteRange nextSector);
    uint32_t DecodeFat12(uint32_t cluster, uint16_t pair);
    void WriteEntry(FatType type, uint32_t cluster, uint32_t value, infra::ByteRange sector, infra::ByteRange nextSector);
    void WriteStraddlingPart(uint32_t cluster, uint32_t value, infra::ByteRange sector, bool second);

    bool IsEndOfChain(FatType type, uint32_t value);
    bool IsBadCluster(FatType type, uint32_t value);
    uint32_t EndOfChain(FatType type);
    uint32_t MediaEntry(FatType type, uint8_t media);

    std::optional<bool> CleanShutdown(FatType type, uint32_t secondEntry);
    uint32_t WithCleanShutdown(FatType type, uint32_t secondEntry, bool clean);
}

#endif
