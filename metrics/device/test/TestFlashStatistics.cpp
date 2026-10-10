#include "metrics/device/FlashStatistics.hpp"
#include "gmock/gmock.h"
#include <chrono>

using namespace std::chrono_literals;

TEST(FlashStatisticsTest, difference_of_counters_is_taken_per_field)
{
    metrics::FlashCounters earlier{ 1, 2, 3, 10, 20, 3, 0, 1us, 2us, 3us };
    metrics::FlashCounters later{ 2, 4, 6, 15, 40, 6, 1, 3us, 5us, 9us };

    EXPECT_EQ((metrics::FlashCounters{ 1, 2, 3, 5, 20, 3, 1, 2us, 3us, 6us }), later - earlier);
}

TEST(FlashStatisticsTest, device_time_is_the_sum_of_read_program_and_erase_time)
{
    metrics::FlashCounters counters{};
    counters.readTime = 1us;
    counters.programTime = 20us;
    counters.eraseTime = 300us;

    EXPECT_EQ(321us, counters.DeviceTime());
}

TEST(FlashStatisticsTest, wear_summary_reports_minimum_maximum_and_mean)
{
    auto summary = metrics::SummarizeWear({ 2, 4, 6, 8 });

    EXPECT_EQ(2, summary.minimum);
    EXPECT_EQ(8, summary.maximum);
    EXPECT_DOUBLE_EQ(5.0, summary.mean);
    EXPECT_DOUBLE_EQ(1.6, summary.Spread());
    EXPECT_DOUBLE_EQ(0.625, summary.Efficiency());
}

TEST(FlashStatisticsTest, wear_summary_of_uniform_counts_has_spread_and_efficiency_one)
{
    auto summary = metrics::SummarizeWear({ 7, 7, 7 });

    EXPECT_DOUBLE_EQ(1.0, summary.Spread());
    EXPECT_DOUBLE_EQ(1.0, summary.Efficiency());
}

TEST(FlashStatisticsTest, wear_summary_without_erases_has_spread_and_efficiency_one)
{
    auto summary = metrics::SummarizeWear({ 0, 0 });

    EXPECT_EQ(0, summary.maximum);
    EXPECT_DOUBLE_EQ(1.0, summary.Spread());
    EXPECT_DOUBLE_EQ(1.0, summary.Efficiency());
}
