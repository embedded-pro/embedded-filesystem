#ifndef VALIDATION_FIRMWARE_BOARDS_MEMORIES_HPP
#define VALIDATION_FIRMWARE_BOARDS_MEMORIES_HPP

#include "infra/util/MemoryRange.hpp"
#include <cstdint>

namespace validation
{
    enum class MemoryType : uint8_t
    {
        device,
        normalCacheable
    };

    struct MemoryRegion
    {
        uint32_t address;
        uint32_t size;
        MemoryType type;
    };

    void ConfigureMemories(infra::MemoryRange<const MemoryRegion> regions);
}

#endif
