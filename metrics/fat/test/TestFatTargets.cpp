#include "metrics/device/DeviceModels.hpp"
#include "metrics/fat/FatTargets.hpp"
#include "gtest/gtest.h"
#include <chrono>
#include <string>
#include <string_view>

namespace
{
    std::string TargetOf(const metrics::Targets& targets, std::string_view metric, std::string_view workload)
    {
        auto target = targets.For(metric, workload);
        return target.has_value() ? target->Text() : "reported";
    }

    class FatTargetsTest
        : public testing::Test
    {
    public:
        std::string TargetOf(std::string_view metric, std::string_view workload) const
        {
            return ::TargetOf(targets, metric, workload);
        }

        metrics::FatTargets targets{ metrics::sdCard256m };
    };
}

TEST_F(FatTargetsTest, throughput_is_at_least_that_of_the_best_baseline)
{
    EXPECT_EQ(">= baselines", TargetOf("W-1", "L1 1048576 64"));
    EXPECT_EQ(">= baselines", TargetOf("R-1", "L6 sequential"));
}

TEST_F(FatTargetsTest, scaling_amplification_and_random_reads_have_bounds)
{
    EXPECT_EQ("<= 1.1", TargetOf("W-2", "L2"));
    EXPECT_EQ("<= 1.2", TargetOf("W-3", "L2"));
    EXPECT_EQ("<= 1.1", TargetOf("W-4", "L1 4194304 4096"));
    EXPECT_EQ("<= 1", TargetOf("R-2", "L6 random 65536"));
    EXPECT_EQ("<= 1.1", TargetOf("R-5", "L6 sequential"));
}

TEST_F(FatTargetsTest, maximum_call_latency_is_one_commit_with_two_allocation_unit_openings)
{
    EXPECT_EQ("<= 1021.025", TargetOf("W-6", "L2 maximum"));
    EXPECT_EQ("<= 1021.025", TargetOf("W-6", "L3 maximum"));
}

TEST_F(FatTargetsTest, commit_bound_follows_the_timings_of_the_device)
{
    metrics::BlockDeviceModel slower{ metrics::sdCard256m };
    slower.maximum.writeBusy = std::chrono::milliseconds(100);
    slower.maximum.allocationUnitOpening = std::chrono::milliseconds(5);

    EXPECT_EQ("<= 411.025", ::TargetOf(metrics::FatTargets{ slower }, "W-6", "L2 maximum"));
}

TEST_F(FatTargetsTest, device_metrics_compare_with_fatfs)
{
    EXPECT_EQ(">= fatfs", TargetOf("M-1", "L1 4194304 4096"));
    EXPECT_EQ(">= fatfs", TargetOf("M-1", "L2"));
    EXPECT_EQ(">= fatfs", TargetOf("M-2", "L1 4194304 64"));
    EXPECT_EQ("<= fatfs/tuned", TargetOf("M-3", "L1 1048576 64"));
    EXPECT_EQ("<= fatfs/tuned", TargetOf("M-3", "L2"));
    EXPECT_EQ("<= fatfs/tuned", TargetOf("M-3", "L4"));
}

TEST_F(FatTargetsTest, device_metrics_that_the_second_fat_copy_costs_are_reported)
{
    EXPECT_EQ("reported", TargetOf("M-1", "L4"));
    EXPECT_EQ("reported", TargetOf("M-2", "L1 1048576 64"));
}

TEST_F(FatTargetsTest, metrics_without_a_fat_target_are_reported)
{
    EXPECT_EQ("reported", TargetOf("W-4", "L2"));
    EXPECT_EQ("reported", TargetOf("W-5", "L3"));
    EXPECT_EQ("reported", TargetOf("W-6", "L2 p99"));
    EXPECT_EQ("reported", TargetOf("R-3", "L6 lookup 1000"));
    EXPECT_EQ("reported", TargetOf("R-4", "L7"));
    EXPECT_EQ("reported", TargetOf("M-2", "L2"));
    EXPECT_EQ("reported", TargetOf("M-2", "L4"));
}

TEST_F(FatTargetsTest, every_power_cut_leaves_a_consistent_volume)
{
    EXPECT_EQ(">= 1", TargetOf("P-1", "append_and_sync reordered"));
}
