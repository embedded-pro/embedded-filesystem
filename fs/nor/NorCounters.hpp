#pragma once

#include <cstdint>

namespace fs
{
    struct NorCounters
    {
        uint32_t allocatedBlocks{ 0 };
        uint32_t erasedSectors{ 0 };
        uint32_t checkpoints{ 0 };
        uint32_t movedBlocks{ 0 };

        bool operator==(const NorCounters& other) const = default;
    };
}
