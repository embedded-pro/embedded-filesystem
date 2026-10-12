#ifndef FS_FAT_FAT_PARTITION_TABLE_HPP
#define FS_FAT_FAT_PARTITION_TABLE_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct PartitionEntry
    {
        uint8_t type{ 0 };
        uint32_t firstSector{ 0 };
        uint32_t sectors{ 0 };

        bool operator==(const PartitionEntry& other) const = default;
    };

    std::optional<PartitionEntry> FirstFatPartition(infra::ConstByteRange sector);
    bool IsProtectiveMbr(infra::ConstByteRange sector);
    void SerializeMbr(const PartitionEntry& partition, infra::ByteRange sector);
}

#endif
