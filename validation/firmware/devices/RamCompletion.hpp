#ifndef VALIDATION_FIRMWARE_DEVICES_RAM_COMPLETION_HPP
#define VALIDATION_FIRMWARE_DEVICES_RAM_COMPLETION_HPP

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

#endif
