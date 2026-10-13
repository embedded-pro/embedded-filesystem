#ifndef FS_FAT_FAT_VOLUME_STATE_HPP
#define FS_FAT_FAT_VOLUME_STATE_HPP

#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatGeometry.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatVolumeState
    {
        FatGeometry geometry;
        std::optional<FsInfo> fsInfo;
        std::optional<bool> cleanAtMount;
        std::optional<uint32_t> freeClusters;
    };
}

#endif
