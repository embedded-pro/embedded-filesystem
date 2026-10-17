#ifndef VALIDATION_FIRMWARE_TIMING_NANOSECONDS_HPP
#define VALIDATION_FIRMWARE_TIMING_NANOSECONDS_HPP

#include <cstdint>

namespace validation
{
    constexpr uint64_t Nanoseconds(uint64_t cycles, uint32_t clockHz)
    {
        return cycles / clockHz * 1'000'000'000 + cycles % clockHz * 1'000'000'000 / clockHz;
    }
}

#endif
