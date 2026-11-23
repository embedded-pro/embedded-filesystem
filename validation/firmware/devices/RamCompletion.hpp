#pragma once

#include <cstdint>

namespace validation
{
    enum class RamCompletion : uint8_t
    {
        scheduled,
        // For the blocking glue of FatFs and littlefs, which then never waits: EmIL cannot dispatch from within an action
        immediate
    };
}
