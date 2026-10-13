#ifndef FS_FAT_FAT_RANKS_HPP
#define FS_FAT_FAT_RANKS_HPP

#include <cstdint>

namespace fs
{
    constexpr uint8_t dataRank = 0;
    constexpr uint8_t tableRank = 1;
    constexpr uint8_t entryRank = 2;
    constexpr uint8_t trailingEntryRank = 3;
    constexpr uint8_t cleanRank = 4;
}

#endif
