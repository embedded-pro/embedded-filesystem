#include "fs/fat/FatTimestamp.hpp"
#include "infra/timer/PartitionedTime.hpp"
#include "gtest/gtest.h"
#include <chrono>

namespace
{
    infra::TimePoint At(uint16_t year, uint8_t month, uint8_t day, uint8_t hours, uint8_t minutes, int8_t seconds)
    {
        return infra::PartitionedTime(year, month, day, hours, minutes, seconds).ToTimePoint();
    }
}

TEST(FatTimestampTest, date_and_time_are_packed)
{
    EXPECT_EQ((fs::FatDateTime{ static_cast<uint16_t>((46 << 9) | (10 << 5) | 12), static_cast<uint16_t>((16 << 11) | (21 << 5) | 17) }), fs::ToFatDateTime(At(2026, 10, 12, 16, 21, 34)));
}

TEST(FatTimestampTest, seconds_are_stored_in_steps_of_two)
{
    EXPECT_EQ(fs::ToFatDateTime(At(2026, 10, 12, 16, 21, 34)), fs::ToFatDateTime(At(2026, 10, 12, 16, 21, 35)));
    EXPECT_NE(fs::ToFatDateTime(At(2026, 10, 12, 16, 21, 34)), fs::ToFatDateTime(At(2026, 10, 12, 16, 21, 36)));
}

TEST(FatTimestampTest, epoch_of_the_system_timer_service_gives_1980)
{
    EXPECT_EQ((fs::FatDateTime{ static_cast<uint16_t>((1 << 5) | 1), 0 }), fs::ToFatDateTime(infra::TimePoint()));
}

TEST(FatTimestampTest, times_before_1980_are_clamped)
{
    EXPECT_EQ(fs::ToFatDateTime(At(1980, 1, 1, 0, 0, 0)), fs::ToFatDateTime(At(1979, 12, 31, 23, 59, 59)));
}

TEST(FatTimestampTest, times_after_2107_are_clamped)
{
    EXPECT_EQ((fs::FatDateTime{ static_cast<uint16_t>((127 << 9) | (12 << 5) | 31), static_cast<uint16_t>((23 << 11) | (59 << 5) | 29) }), fs::ToFatDateTime(At(2108, 1, 1, 0, 0, 0)));
    EXPECT_EQ(fs::ToFatDateTime(At(2108, 1, 1, 0, 0, 0)), fs::ToFatDateTime(At(2107, 12, 31, 23, 59, 59)));
}
