#include "fs/fat/FatFreeRuns.hpp"
#include "gtest/gtest.h"
#include <array>

namespace
{
    class RangeReservations
        : public fs::FatReservations
    {
    public:
        bool Reserved(uint32_t cluster) const override
        {
            return reserved.Contains(cluster);
        }

        fs::FatReservation reserved;
    };

    class FatFreeRunsTest
        : public testing::Test
    {
    public:
        void Found(uint32_t first, uint32_t count)
        {
            for (auto cluster = first; cluster != first + count; ++cluster)
                runs.Extend(cluster);

            runs.Close();
        }

        std::optional<uint32_t> FirstFrom(uint32_t from) const
        {
            return runs.WholeUnit(fs::FatAllocationUnits{ 1, 2 }, from, reservations);
        }

        std::optional<uint32_t> UnitFrom(uint32_t from) const
        {
            return runs.WholeUnit(fs::FatAllocationUnits{ 8, 4 }, from, reservations);
        }

        std::array<fs::FatReservation, 3> storage{};
        fs::FatFreeRuns runs{ infra::MakeRange(storage) };
        RangeReservations reservations;
    };
}

TEST_F(FatFreeRunsTest, nothing_is_known_before_a_run_is_found)
{
    EXPECT_EQ(std::nullopt, FirstFrom(2));
}

TEST_F(FatFreeRunsTest, consecutive_free_clusters_form_one_run)
{
    Found(10, 5);

    EXPECT_EQ(10, FirstFrom(2));
    EXPECT_EQ(14, FirstFrom(14));
    EXPECT_EQ(10, FirstFrom(15));
}

TEST_F(FatFreeRunsTest, run_that_is_still_being_found_is_known)
{
    runs.Extend(10);
    runs.Extend(11);

    EXPECT_EQ(11, FirstFrom(11));
}

TEST_F(FatFreeRunsTest, gap_starts_a_new_run)
{
    runs.Extend(10);
    runs.Extend(12);

    EXPECT_EQ(10, FirstFrom(2));
    EXPECT_EQ(12, FirstFrom(11));
}

TEST_F(FatFreeRunsTest, longest_runs_are_kept)
{
    Found(10, 1);
    Found(20, 4);
    Found(30, 2);
    Found(40, 3);
    Found(50, 1);

    EXPECT_EQ(20, FirstFrom(2));
    EXPECT_EQ(30, FirstFrom(24));
    EXPECT_EQ(40, FirstFrom(32));
    EXPECT_EQ(20, FirstFrom(43));
}

TEST_F(FatFreeRunsTest, taken_cluster_splits_its_run)
{
    Found(10, 5);

    runs.Take(12);

    EXPECT_EQ(11, FirstFrom(11));
    EXPECT_EQ(13, FirstFrom(12));
}

TEST_F(FatFreeRunsTest, taking_the_first_or_last_cluster_shortens_the_run)
{
    Found(10, 3);

    runs.Take(10);
    runs.Take(12);

    EXPECT_EQ(11, FirstFrom(2));
    EXPECT_EQ(11, FirstFrom(12));
}

TEST_F(FatFreeRunsTest, part_after_a_split_is_dropped_when_no_kept_run_is_shorter)
{
    Found(10, 5);
    Found(20, 6);
    Found(30, 7);

    runs.Take(12);

    EXPECT_EQ(10, FirstFrom(2));
    EXPECT_EQ(20, FirstFrom(12));
}

TEST_F(FatFreeRunsTest, taken_cluster_of_the_run_being_found_keeps_the_part_before_it)
{
    runs.Extend(10);
    runs.Extend(11);
    runs.Extend(12);

    runs.Take(11);
    runs.Extend(13);

    EXPECT_EQ(10, FirstFrom(2));
    EXPECT_EQ(12, FirstFrom(11));
    EXPECT_EQ(13, FirstFrom(13));
}

TEST_F(FatFreeRunsTest, taking_an_unknown_cluster_changes_nothing)
{
    Found(10, 2);

    runs.Take(50);

    EXPECT_EQ(10, FirstFrom(2));
    EXPECT_EQ(11, FirstFrom(11));
}

TEST_F(FatFreeRunsTest, clear_forgets_every_run)
{
    Found(10, 2);
    runs.Extend(20);

    runs.Clear();

    EXPECT_EQ(std::nullopt, FirstFrom(2));
}

TEST_F(FatFreeRunsTest, whole_unit_starts_at_a_boundary_inside_a_run)
{
    Found(6, 20);

    EXPECT_EQ(12, UnitFrom(2));
    EXPECT_EQ(12, UnitFrom(13));
}

TEST_F(FatFreeRunsTest, unit_that_ends_past_its_run_is_not_whole)
{
    Found(12, 7);

    EXPECT_EQ(std::nullopt, UnitFrom(2));
}

TEST_F(FatFreeRunsTest, unit_split_over_two_runs_is_not_whole)
{
    Found(12, 3);
    Found(16, 5);

    EXPECT_EQ(std::nullopt, UnitFrom(2));
}

TEST_F(FatFreeRunsTest, first_unit_at_or_after_the_start_is_chosen)
{
    Found(4, 32);

    EXPECT_EQ(4, UnitFrom(4));
    EXPECT_EQ(20, UnitFrom(13));
    EXPECT_EQ(4, UnitFrom(29));
}

TEST_F(FatFreeRunsTest, unit_with_a_reserved_cluster_is_skipped)
{
    Found(4, 24);
    reservations.reserved = fs::FatReservation{ 14, 1 };

    EXPECT_EQ(20, UnitFrom(12));
}

TEST_F(FatFreeRunsTest, unit_of_the_run_being_found_is_known)
{
    for (uint32_t cluster = 12; cluster != 20; ++cluster)
        runs.Extend(cluster);

    EXPECT_EQ(12, UnitFrom(2));
}

TEST(FatFreeRunsWithoutStorageTest, only_the_run_being_found_is_known)
{
    RangeReservations reservations;
    fs::FatFreeRuns runs{ infra::MemoryRange<fs::FatReservation>() };
    runs.Extend(10);

    EXPECT_EQ(10, runs.WholeUnit(fs::FatAllocationUnits{ 1, 2 }, 2, reservations));

    runs.Close();

    EXPECT_EQ(std::nullopt, runs.WholeUnit(fs::FatAllocationUnits{ 1, 2 }, 2, reservations));
}
