#include "validation/firmware/workload/AppendWindows.hpp"
#include "gmock/gmock.h"
#include <cstdint>
#include <optional>

namespace
{
    using validation::WorkloadOperation;

    validation::AppendCall Call(WorkloadOperation operation, uint64_t programNs, uint64_t checkpoints = 0, uint32_t fileSizeBefore = 0)
    {
        validation::FlashCounts device;
        device.programNs = programNs;
        validation::VolumeCounts volume;
        volume.checkpoints = checkpoints;
        return { operation, fileSizeBefore, device, volume };
    }

    void AddPeriods(validation::LastPeriods& periods, uint64_t first, uint64_t count)
    {
        for (auto period = first; period != first + count; ++period)
        {
            periods.Add(Call(WorkloadOperation::write, period));
            periods.Add(Call(WorkloadOperation::sync, 10 * period, 1));
        }
    }
}

TEST(WindowTotalsTest, counts_the_writes_as_appends_and_adds_the_counts_of_every_call)
{
    validation::WindowTotals totals;

    totals.Add(Call(WorkloadOperation::write, 2));
    totals.Add(Call(WorkloadOperation::sync, 3, 1));
    totals.Add(Call(WorkloadOperation::idle, 4));

    EXPECT_EQ(1, totals.appends);
    EXPECT_EQ(9, totals.device.programNs);
    EXPECT_EQ(1, totals.volume.checkpoints);
}

TEST(LastPeriodsTest, has_no_window_before_four_periods_follow_a_checkpoint)
{
    validation::LastPeriods periods;

    AddPeriods(periods, 1, 4);

    EXPECT_EQ(std::nullopt, periods.Window());
}

TEST(LastPeriodsTest, the_window_holds_the_calls_after_a_checkpoint_until_the_fourth_checkpoint_after_it)
{
    validation::LastPeriods periods;
    periods.Add(Call(WorkloadOperation::write, 1000));
    periods.Add(Call(WorkloadOperation::sync, 1000, 1));

    AddPeriods(periods, 1, 4);
    periods.Add(Call(WorkloadOperation::write, 1000));

    auto window = periods.Window();
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(4, window->appends);
    EXPECT_EQ(1 + 2 + 3 + 4 + 10 + 20 + 30 + 40, window->device.programNs);
    EXPECT_EQ(4, window->volume.checkpoints);
}

TEST(LastPeriodsTest, the_window_holds_the_last_four_periods)
{
    validation::LastPeriods periods;
    periods.Add(Call(WorkloadOperation::sync, 1000, 1));

    AddPeriods(periods, 1, 6);

    EXPECT_EQ(3 + 4 + 5 + 6 + 30 + 40 + 50 + 60, periods.Window()->device.programNs);
}

TEST(LastPeriodsTest, a_call_with_two_checkpoints_ends_two_periods)
{
    validation::LastPeriods periods;
    periods.Add(Call(WorkloadOperation::sync, 1000, 1));
    AddPeriods(periods, 1, 2);

    periods.Add(Call(WorkloadOperation::sync, 7, 2));

    EXPECT_EQ(1 + 2 + 10 + 20 + 7, periods.Window()->device.programNs);
    EXPECT_EQ(4, periods.Window()->volume.checkpoints);
}

TEST(LastPeriodsTest, has_no_window_when_cleared)
{
    validation::LastPeriods periods;
    periods.Add(Call(WorkloadOperation::sync, 1000, 1));
    AddPeriods(periods, 1, 4);

    periods.Clear();
    AddPeriods(periods, 1, 4);

    EXPECT_EQ(std::nullopt, periods.Window());
}

TEST(PeriodsFromTest, the_window_starts_after_the_first_checkpoint_once_a_write_starts_at_the_size)
{
    validation::PeriodsFrom periods{ 4096 };
    periods.Add(Call(WorkloadOperation::sync, 1000, 1));
    periods.Add(Call(WorkloadOperation::write, 1000, 1, 4032));
    periods.Add(Call(WorkloadOperation::sync, 1000, 1, 4096));
    periods.Add(Call(WorkloadOperation::write, 1000, 0, 4096));
    periods.Add(Call(WorkloadOperation::sync, 1000, 1));

    for (uint64_t period = 1; period != 5; ++period)
    {
        periods.Add(Call(WorkloadOperation::write, period));
        periods.Add(Call(WorkloadOperation::sync, 10 * period, 1));
    }
    periods.Add(Call(WorkloadOperation::write, 1000));

    auto window = periods.Window();
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(4, window->appends);
    EXPECT_EQ(1 + 2 + 3 + 4 + 10 + 20 + 30 + 40, window->device.programNs);
}

TEST(PeriodsFromTest, the_write_at_the_size_may_carry_the_first_checkpoint)
{
    validation::PeriodsFrom periods{ 4096 };
    periods.Add(Call(WorkloadOperation::write, 1000, 1, 5000));

    periods.Add(Call(WorkloadOperation::write, 5, 4));

    EXPECT_EQ(5, periods.Window()->device.programNs);
}

TEST(PeriodsFromTest, has_no_window_before_four_checkpoints)
{
    validation::PeriodsFrom periods{ 4096 };
    periods.Add(Call(WorkloadOperation::write, 1000, 1, 4096));

    periods.Add(Call(WorkloadOperation::write, 5, 3));

    EXPECT_EQ(std::nullopt, periods.Window());
}

TEST(PeriodsFromTest, has_no_window_when_cleared)
{
    validation::PeriodsFrom periods{ 4096 };
    periods.Add(Call(WorkloadOperation::write, 1000, 1, 4096));
    periods.Add(Call(WorkloadOperation::write, 5, 4));

    periods.Clear();
    periods.Add(Call(WorkloadOperation::write, 5, 4));

    EXPECT_EQ(std::nullopt, periods.Window());
}
