#include "metrics/nor/NorTargets.hpp"
#include "gtest/gtest.h"
#include <string>
#include <string_view>

namespace
{
    std::string TargetOf(std::string_view metric, std::string_view workload)
    {
        metrics::NorTargets targets;
        auto target = targets.For(metric, workload);
        return target.has_value() ? target->Text() : "reported";
    }
}

TEST(NorTargetsTest, every_power_cut_in_every_mode_leaves_a_consistent_volume)
{
    EXPECT_EQ(">= 1", TargetOf("P-1", "format notStarted"));
    EXPECT_EQ(">= 1", TargetOf("P-1", "format partial"));
    EXPECT_EQ(">= 1", TargetOf("P-1", "format completed"));
}

TEST(NorTargetsTest, other_metrics_are_reported_until_their_targets_are_attached)
{
    EXPECT_EQ("reported", TargetOf("W-1", "L1 1048576 64"));
    EXPECT_EQ("reported", TargetOf("W-6", "L2 maximum"));
    EXPECT_EQ("reported", TargetOf("WL-1", "L5"));
}
