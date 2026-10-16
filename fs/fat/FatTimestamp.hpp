#ifndef FS_FAT_FAT_TIMESTAMP_HPP
#define FS_FAT_FAT_TIMESTAMP_HPP

#include "infra/timer/Timer.hpp"
#include <cstdint>

namespace fs
{
    struct FatDateTime
    {
        uint16_t date{ 0 };
        uint16_t time{ 0 };

        bool operator==(const FatDateTime& other) const = default;
    };

    constexpr FatDateTime fatEpoch{ (1 << 5) | 1, 0 };

    FatDateTime ToFatDateTime(infra::TimePoint time);
    FatDateTime FatNow(uint32_t timerServiceId);
}

#endif
