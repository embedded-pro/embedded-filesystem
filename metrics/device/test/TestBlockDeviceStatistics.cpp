#include "metrics/device/BlockDeviceStatistics.hpp"
#include "gmock/gmock.h"
#include <chrono>

using namespace std::chrono_literals;

TEST(BlockDeviceStatisticsTest, difference_of_counters_is_taken_per_field)
{
    metrics::BlockDeviceCounters earlier{ 1, 1, 1, 1, 1, 10, 10, 10, 1, 1, 1us, 1us, 1us, 1us };
    metrics::BlockDeviceCounters later{ 3, 4, 5, 6, 2, 20, 30, 40, 3, 4, 2us, 3us, 4us, 5us };

    EXPECT_EQ((metrics::BlockDeviceCounters{ 2, 3, 4, 5, 1, 10, 20, 30, 2, 3, 1us, 2us, 3us, 4us }), later - earlier);
}

TEST(BlockDeviceStatisticsTest, device_time_is_the_sum_of_every_kind_of_request)
{
    metrics::BlockDeviceCounters counters{};
    counters.readTime = 1us;
    counters.writeTime = 20us;
    counters.discardTime = 300us;
    counters.flushTime = 4000us;

    EXPECT_EQ(4321us, counters.DeviceTime());
}

TEST(BlockDeviceStatisticsTest, mean_blocks_per_write_divides_blocks_written_by_write_requests)
{
    metrics::BlockDeviceCounters counters{};
    counters.writeRequests = 4;
    counters.blocksWritten = 10;

    EXPECT_DOUBLE_EQ(2.5, counters.MeanBlocksPerWrite());
}

TEST(BlockDeviceStatisticsTest, aligned_share_divides_aligned_blocks_by_blocks_written)
{
    metrics::BlockDeviceCounters counters{};
    counters.writeRequests = 1;
    counters.blocksWritten = 8;
    counters.alignedBlocks = 6;

    EXPECT_DOUBLE_EQ(0.75, counters.AlignedShare());
}

TEST(BlockDeviceStatisticsTest, ratios_without_writes_are_zero)
{
    metrics::BlockDeviceCounters counters{};

    EXPECT_DOUBLE_EQ(0.0, counters.MeanBlocksPerWrite());
    EXPECT_DOUBLE_EQ(0.0, counters.AlignedShare());
}
