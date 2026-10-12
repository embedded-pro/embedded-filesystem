#ifndef FS_FAT_CODE_PAGE_437_HPP
#define FS_FAT_CODE_PAGE_437_HPP

#include <cstdint>

namespace fs
{
    char32_t FromCodePage437(uint8_t byte);
}

#endif
