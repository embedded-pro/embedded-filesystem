#ifndef METRICS_NOR_NOR_CONFIGURATIONS_HPP
#define METRICS_NOR_NOR_CONFIGURATIONS_HPP

#include "fs/nor/NorConfig.hpp"

namespace metrics
{
    inline constexpr fs::NorConfig norQspi16mConfig{ .maxBlocks = 4096, .programUnit = 1, .pageSize = 256 };
    inline constexpr fs::NorConfig norQspi1mConfig{ .maxBlocks = 256, .programUnit = 1, .pageSize = 256 };
    // With blocks of one 2 KiB sector and names of 64 bytes, a block holds 3 nodes of 3 inode records each,
    // and the 1000 files of L6 need more node blocks than the collection can keep at twice the live nodes
    inline constexpr fs::NorConfig internalFlashConfig{ .maxBlocks = 256, .sectorsPerBlock = 2, .programUnit = 16, .pageSize = 16, .maxNameBytes = 32 };
}

#endif
