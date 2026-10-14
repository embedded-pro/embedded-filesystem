#ifndef FS_NOR_NOR_OPEN_FILE_HPP
#define FS_NOR_NOR_OPEN_FILE_HPP

#include "fs/interfaces/OpenMode.hpp"
#include <cstdint>

namespace fs
{
    struct NorOpenFile
    {
        uint32_t inode{ 0 };
        uint32_t size{ 0 };
        uint32_t position{ 0 };
        OpenMode mode{ OpenMode::read };
    };
}

#endif
