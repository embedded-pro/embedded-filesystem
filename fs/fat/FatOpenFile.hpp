#ifndef FS_FAT_FAT_OPEN_FILE_HPP
#define FS_FAT_FAT_OPEN_FILE_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/interfaces/OpenMode.hpp"
#include <cstdint>

namespace fs
{
    struct FatOpenFile
    {
        FatEntryPosition entry{};
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        uint32_t position{ 0 };
        OpenMode mode{ OpenMode::read };
    };
}

#endif
