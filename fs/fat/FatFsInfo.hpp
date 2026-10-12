#ifndef FS_FAT_FAT_FS_INFO_HPP
#define FS_FAT_FAT_FS_INFO_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FsInfo
    {
        std::optional<uint32_t> freeClusters;
        std::optional<uint32_t> nextFree;

        bool operator==(const FsInfo& other) const = default;
    };

    std::optional<FsInfo> ParseFsInfo(infra::ConstByteRange sector, uint32_t clusterCount);
    void SerializeFsInfo(const FsInfo& info, infra::ByteRange sector);
}

#endif
