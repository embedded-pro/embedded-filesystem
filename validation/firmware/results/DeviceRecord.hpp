#ifndef VALIDATION_FIRMWARE_RESULTS_DEVICE_RECORD_HPP
#define VALIDATION_FIRMWARE_RESULTS_DEVICE_RECORD_HPP

#include "infra/util/BoundedString.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include <cstdint>

namespace validation
{
    enum class DeviceKind : uint8_t
    {
        nor,
        block
    };

    struct DeviceDescription
    {
        infra::BoundedConstString name;
        DeviceKind kind;
        uint32_t units;
        uint32_t unitBytes;
        uint32_t pageBytes;
        infra::BoundedConstString chip;
        uint32_t clockHz;
        infra::BoundedConstString memory;
        uint32_t unitBlocks{ 0 };
        infra::BoundedConstString cache{};
    };

    void PrintDevice(ResultLines& results, const DeviceDescription& device);
}

#endif
