#include "metrics/device/DeviceModels.hpp"
#include "metrics/nor/NorConfigurations.hpp"
#include "metrics/nor/NorTargets.hpp"
#include "gtest/gtest.h"
#include <string>
#include <string_view>

namespace
{
    const metrics::NorTargets norQspi16m{ metrics::norQspi16m, metrics::norQspi16mConfig, 1 };
    const metrics::NorTargets internalFlash{ metrics::internalFlash, metrics::internalFlashConfig, 64 };
    const metrics::NorTargets lifetime{ metrics::norQspi1m, metrics::norQspi1mConfig, 1, metrics::WearTargets::asserted };
    const metrics::NorTargets shortRunOnNorQspi1m{ metrics::norQspi1m, metrics::norQspi1mConfig, 1 };

    std::string TargetOf(const metrics::NorTargets& targets, std::string_view metric, std::string_view workload)
    {
        auto target = targets.For(metric, workload);
        return target.has_value() ? target->Text() : "reported";
    }
}

TEST(NorTargetsTest, every_power_cut_in_every_mode_leaves_a_consistent_volume)
{
    EXPECT_EQ(">= 1", TargetOf(norQspi16m, "P-1", "format notStarted"));
    EXPECT_EQ(">= 1", TargetOf(internalFlash, "P-1", "leveling-data partial"));
}

TEST(NorTargetsTest, sequential_writes_reach_0_97_times_the_best_baseline)
{
    EXPECT_EQ(">= 0.97 baselines", TargetOf(norQspi16m, "W-1", "L1 1048576 64"));
    EXPECT_EQ(">= 0.97 baselines", TargetOf(internalFlash, "W-1", "L1 65536 4096"));
}

TEST(NorTargetsTest, sequential_writes_of_the_smaller_files_are_reported_where_the_files_are_divided)
{
    EXPECT_EQ("reported", TargetOf(internalFlash, "W-1", "L1 16384 64"));
    EXPECT_EQ("reported", TargetOf(internalFlash, "W-1", "L1 16384 4096"));
}

TEST(NorTargetsTest, appends_and_overwrites_scale_with_the_file_size_and_the_fill_level)
{
    EXPECT_EQ("<= 1.1", TargetOf(norQspi16m, "W-2", "L2"));
    EXPECT_EQ("<= 1.2", TargetOf(norQspi16m, "W-3", "L2"));
    EXPECT_EQ("<= 1.2", TargetOf(internalFlash, "W-5", "L3"));
}

TEST(NorTargetsTest, append_scaling_is_reported_where_the_logs_are_divided)
{
    EXPECT_EQ("reported", TargetOf(internalFlash, "W-2", "L2"));
}

TEST(NorTargetsTest, sequential_writes_amplify_by_at_most_1_1_and_the_data_logger_is_reported)
{
    EXPECT_EQ("<= 1.1", TargetOf(norQspi16m, "W-4", "L1 1048576 64"));
    EXPECT_EQ("<= 1.1", TargetOf(internalFlash, "W-4", "L1 16384 512"));
    EXPECT_EQ("reported", TargetOf(norQspi16m, "W-4", "L2"));
}

TEST(NorTargetsTest, the_first_phase_of_l1_is_reported_where_the_files_are_divided)
{
    EXPECT_EQ("reported", TargetOf(internalFlash, "W-4", "L1 16384 64"));
}

TEST(NorTargetsTest, the_longest_append_is_bounded_by_one_commit_and_overwrites_are_reported)
{
    EXPECT_EQ(metrics::AtMost(internalFlash.CommitMilliseconds()).Text(), TargetOf(internalFlash, "W-6", "L2 maximum"));
    EXPECT_EQ("reported", TargetOf(internalFlash, "W-6", "L2 p99"));
    EXPECT_EQ("reported", TargetOf(internalFlash, "W-6", "L3 maximum"));
}

TEST(NorTargetsTest, one_commit_is_a_sector_erase_a_move_that_finishes_a_block_and_erases_its_destination_the_copy_of_two_blocks_and_two_records)
{
    EXPECT_NEAR(87.03, internalFlash.CommitMilliseconds(), 0.05);
}

TEST(NorTargetsTest, reads_reach_the_best_baseline_with_at_most_4_requests_per_random_read)
{
    EXPECT_EQ(">= baselines", TargetOf(norQspi16m, "R-1", "L6 sequential"));
    EXPECT_EQ("<= 4", TargetOf(norQspi16m, "R-2", "L6 random 4194304"));
    EXPECT_EQ("<= 4", TargetOf(internalFlash, "R-2", "L6 random 1024"));
    EXPECT_EQ("<= 1.1", TargetOf(norQspi16m, "R-5", "L6 sequential"));
    EXPECT_EQ("reported", TargetOf(norQspi16m, "R-3", "L6 lookup 1000"));
    EXPECT_EQ("reported", TargetOf(norQspi16m, "R-4", "L7"));
}

TEST(NorTargetsTest, the_ratio_of_random_reads_is_asserted_where_the_files_keep_their_size)
{
    EXPECT_EQ("<= 1.2", TargetOf(norQspi16m, "R-2", "L6 random ratio"));
    EXPECT_EQ("reported", TargetOf(internalFlash, "R-2", "L6 random ratio"));
}

TEST(NorTargetsTest, wear_targets_apply_to_the_lifetime_run)
{
    EXPECT_EQ("<= 1.2", TargetOf(lifetime, "WL-1", "L5"));
    EXPECT_EQ(">= 0.8", TargetOf(lifetime, "WL-2", "L5"));
    EXPECT_EQ("<= 0.05", TargetOf(lifetime, "WL-4", "L5"));
    EXPECT_EQ("reported", TargetOf(lifetime, "WL-3", "L5"));
}

TEST(NorTargetsTest, wear_on_short_runs_is_reported)
{
    EXPECT_EQ("reported", TargetOf(norQspi16m, "WL-1", "L5"));
    EXPECT_EQ("reported", TargetOf(norQspi16m, "WL-2", "L4"));
    EXPECT_EQ("reported", TargetOf(internalFlash, "WL-4", "L5"));
    EXPECT_EQ("reported", TargetOf(shortRunOnNorQspi1m, "WL-1", "L5"));
}
