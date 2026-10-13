#include "fs/fat/FatRunSearch.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <optional>

namespace
{
    class RangeReservations
        : public fs::FatReservations
    {
    public:
        bool Reserved(uint32_t cluster) const override
        {
            return reservation.Contains(cluster);
        }

        fs::FatReservation reservation;
    };

    class FatRunSearchTest
        : public FatVolumeFixture
    {
    public:
        explicit FatRunSearchTest(uint32_t blocks = 16384)
            : FatVolumeFixture(blocks, ClusterSize(1))
        {
            state.geometry = geometry;
        }

        void ExpectFind(std::optional<uint32_t> after, uint32_t cursor, uint32_t wanted, const fs::FatReservation& expected, fs::Result result = fs::Result::success)
        {
            search.Find(after, cursor, wanted, units, [this](fs::Result result, fs::FatReservation run)
                {
                    found.callback(result, run);
                });
            EXPECT_CALL(found, callback(result, expected));
            ExecuteAllActions();
        }

        void Use(uint32_t first, uint32_t count)
        {
            for (auto cluster = first; cluster != first + count; ++cluster)
                builder->SetFatEntry(cluster, fs::EndOfChain(geometry.type));
        }

        fs::FatVolumeState state;
        RangeReservations reservations;
        std::optional<fs::FatAllocationUnits> units;
        fs::FatRunSearch search{ cache, state, reservations };
        testing::StrictMock<infra::MockCallback<void(fs::Result, fs::FatReservation)>> found;
    };

    class FatRunSearchOnFat12Test
        : public FatRunSearchTest
    {
    public:
        FatRunSearchOnFat12Test()
            : FatRunSearchTest(4096)
        {}
    };
}

TEST_F(FatRunSearchTest, run_follows_the_last_cluster_of_the_file)
{
    Use(2, 10);

    ExpectFind(11, 100, 5, fs::FatReservation{ 12, 5 });
}

TEST_F(FatRunSearchTest, run_after_the_last_cluster_that_is_too_short_is_searched_from_the_cursor)
{
    Use(2, 10);
    Use(14, 1);

    ExpectFind(11, 100, 5, fs::FatReservation{ 100, 5 });
}

TEST_F(FatRunSearchTest, search_skips_runs_that_are_too_short)
{
    Use(102, 1);
    Use(110, 1);

    ExpectFind(std::nullopt, 100, 5, fs::FatReservation{ 103, 5 });
}

TEST_F(FatRunSearchTest, reserved_clusters_are_skipped)
{
    reservations.reservation = fs::FatReservation{ 100, 3 };

    ExpectFind(std::nullopt, 100, 5, fs::FatReservation{ 103, 5 });
}

TEST_F(FatRunSearchTest, run_that_would_cross_an_allocation_unit_starts_at_the_boundary)
{
    units = fs::FatAllocationUnits{ 64, 2 };

    ExpectFind(std::nullopt, 125, 10, fs::FatReservation{ 130, 10 });
}

TEST_F(FatRunSearchTest, run_that_ends_at_a_boundary_is_not_moved)
{
    units = fs::FatAllocationUnits{ 64, 2 };

    ExpectFind(std::nullopt, 120, 10, fs::FatReservation{ 120, 10 });
}

TEST_F(FatRunSearchTest, run_inside_an_allocation_unit_is_not_moved)
{
    units = fs::FatAllocationUnits{ 64, 2 };

    ExpectFind(std::nullopt, 100, 20, fs::FatReservation{ 100, 20 });
}

TEST_F(FatRunSearchTest, run_stays_when_the_boundary_is_not_followed_by_enough_free_clusters)
{
    units = fs::FatAllocationUnits{ 64, 2 };
    Use(135, 1);

    ExpectFind(std::nullopt, 125, 10, fs::FatReservation{ 125, 10 });
}

TEST_F(FatRunSearchTest, run_at_a_boundary_is_not_moved)
{
    units = fs::FatAllocationUnits{ 64, 2 };

    ExpectFind(std::nullopt, 130, 100, fs::FatReservation{ 130, 100 });
}

TEST_F(FatRunSearchTest, search_wraps_around_at_the_end_of_the_volume)
{
    auto lastCluster = geometry.clusterCount + 1;
    Use(2, 3);

    ExpectFind(std::nullopt, lastCluster - 1, 5, fs::FatReservation{ 5, 5 });
}

TEST_F(FatRunSearchTest, run_does_not_continue_across_the_end_of_the_volume)
{
    auto lastCluster = geometry.clusterCount + 1;
    Use(2, 1);

    ExpectFind(std::nullopt, lastCluster, 2, fs::FatReservation{ 3, 2 });
}

TEST_F(FatRunSearchTest, run_is_not_moved_to_a_boundary_too_close_to_the_end_of_the_volume)
{
    auto lastCluster = geometry.clusterCount + 1;
    units = fs::FatAllocationUnits{ 64, lastCluster };

    ExpectFind(std::nullopt, lastCluster - 1, 2, fs::FatReservation{ lastCluster - 1, 2 });
}

TEST_F(FatRunSearchTest, longest_run_is_found_when_none_is_long_enough)
{
    for (uint32_t cluster = 2; cluster != geometry.clusterCount + 2; cluster += 4)
        Use(cluster, 1);

    ExpectFind(std::nullopt, 100, 5, fs::FatReservation{ 103, 3 });
}

TEST_F(FatRunSearchTest, run_that_passes_the_start_of_the_search_is_found)
{
    Use(2, geometry.clusterCount);
    for (uint32_t cluster = 97; cluster != 105; ++cluster)
        builder->SetFatEntry(cluster, 0);

    ExpectFind(std::nullopt, 100, 6, fs::FatReservation{ 97, 6 });
}

TEST_F(FatRunSearchTest, full_volume_has_no_run)
{
    Use(2, geometry.clusterCount);

    ExpectFind(std::nullopt, 100, 5, fs::FatReservation{ 0, 0 });
}

TEST_F(FatRunSearchTest, device_error_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectFind(std::nullopt, 100, 5, fs::FatReservation{}, fs::Result::deviceError);
}

TEST_F(FatRunSearchOnFat12Test, clusters_that_straddle_two_sectors_end_a_run)
{
    ASSERT_EQ(fs::FatType::fat12, geometry.type);

    ExpectFind(std::nullopt, 338, 6, fs::FatReservation{ 342, 6 });
}
