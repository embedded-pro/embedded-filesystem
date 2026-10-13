#include "fs/fat/FatExtentCache.hpp"
#include "gtest/gtest.h"

namespace
{
    class FatExtentCacheTest
        : public testing::Test
    {
    public:
        fs::FatExtentCache::WithStorage<3> extents;
    };
}

TEST(FatExtentTest, covers_its_cluster_indices)
{
    fs::FatExtent extent{ 4, 104, 3 };

    EXPECT_FALSE(extent.Covers(3));
    EXPECT_TRUE(extent.Covers(4));
    EXPECT_TRUE(extent.Covers(6));
    EXPECT_FALSE(extent.Covers(7));
    EXPECT_EQ(7u, extent.End());
    EXPECT_EQ(105u, extent.ClusterAt(5));
}

TEST_F(FatExtentCacheTest, empty_cache_covers_nothing)
{
    EXPECT_EQ(std::nullopt, extents.Covering(0));
    EXPECT_EQ(std::nullopt, extents.NearestBefore(10));
    EXPECT_EQ(std::nullopt, extents.Last());
}

TEST_F(FatExtentCacheTest, finds_the_extent_covering_an_index)
{
    extents.Add(fs::FatExtent{ 0, 10, 2 });
    extents.Add(fs::FatExtent{ 2, 50, 3 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 2 }), extents.Covering(1));
    EXPECT_EQ((fs::FatExtent{ 2, 50, 3 }), extents.Covering(4));
    EXPECT_EQ(std::nullopt, extents.Covering(5));
}

TEST_F(FatExtentCacheTest, nearest_before_is_the_extent_that_starts_last_before_an_index)
{
    extents.Add(fs::FatExtent{ 0, 10, 2 });
    extents.Add(fs::FatExtent{ 8, 80, 1 });
    extents.Add(fs::FatExtent{ 4, 40, 1 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 2 }), extents.NearestBefore(3));
    EXPECT_EQ((fs::FatExtent{ 4, 40, 1 }), extents.NearestBefore(7));
    EXPECT_EQ((fs::FatExtent{ 8, 80, 1 }), extents.NearestBefore(20));
}

TEST_F(FatExtentCacheTest, last_is_the_extent_that_ends_last)
{
    extents.Add(fs::FatExtent{ 0, 10, 2 });
    extents.Add(fs::FatExtent{ 8, 80, 1 });
    extents.Add(fs::FatExtent{ 4, 40, 1 });

    EXPECT_EQ((fs::FatExtent{ 8, 80, 1 }), extents.Last());
}

TEST_F(FatExtentCacheTest, longer_run_with_the_same_start_extends_an_extent)
{
    extents.Add(fs::FatExtent{ 0, 10, 1 });
    extents.Add(fs::FatExtent{ 0, 10, 4 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 4 }), extents.Covering(3));
    EXPECT_EQ((fs::FatExtent{ 0, 10, 4 }), extents.Last());
}

TEST_F(FatExtentCacheTest, adjacent_contiguous_runs_merge)
{
    extents.Add(fs::FatExtent{ 0, 10, 2 });
    extents.Add(fs::FatExtent{ 2, 12, 2 });
    extents.Add(fs::FatExtent{ 6, 60, 1 });
    extents.Add(fs::FatExtent{ 5, 59, 1 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 4 }), extents.Covering(3));
    EXPECT_EQ((fs::FatExtent{ 5, 59, 2 }), extents.Covering(6));
}

TEST_F(FatExtentCacheTest, adjacent_runs_that_are_not_contiguous_stay_apart)
{
    extents.Add(fs::FatExtent{ 0, 10, 2 });
    extents.Add(fs::FatExtent{ 2, 20, 2 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 2 }), extents.Covering(1));
    EXPECT_EQ((fs::FatExtent{ 2, 20, 2 }), extents.Covering(2));
}

TEST_F(FatExtentCacheTest, full_cache_keeps_the_first_extent_and_replaces_the_least_recently_used)
{
    extents.Add(fs::FatExtent{ 0, 10, 1 });
    extents.Add(fs::FatExtent{ 1, 20, 1 });
    extents.Add(fs::FatExtent{ 2, 30, 1 });
    extents.Covering(1);

    extents.Add(fs::FatExtent{ 3, 40, 1 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 1 }), extents.Covering(0));
    EXPECT_EQ((fs::FatExtent{ 1, 20, 1 }), extents.Covering(1));
    EXPECT_EQ(std::nullopt, extents.Covering(2));
    EXPECT_EQ((fs::FatExtent{ 3, 40, 1 }), extents.Covering(3));
}

TEST_F(FatExtentCacheTest, clear_forgets_every_extent)
{
    extents.Add(fs::FatExtent{ 0, 10, 1 });

    extents.Clear();

    EXPECT_EQ(std::nullopt, extents.Covering(0));
}

TEST_F(FatExtentCacheTest, truncate_drops_the_extents_after_the_kept_clusters_and_shortens_the_last)
{
    extents.Add(fs::FatExtent{ 0, 10, 2 });
    extents.Add(fs::FatExtent{ 2, 50, 3 });
    extents.Add(fs::FatExtent{ 5, 80, 2 });

    extents.Truncate(3);

    EXPECT_EQ((fs::FatExtent{ 0, 10, 2 }), extents.Covering(0));
    EXPECT_EQ((fs::FatExtent{ 2, 50, 1 }), extents.Covering(2));
    EXPECT_EQ(std::nullopt, extents.Covering(3));
    EXPECT_EQ(std::nullopt, extents.Covering(5));
    EXPECT_EQ((fs::FatExtent{ 2, 50, 1 }), extents.Last());
}

TEST_F(FatExtentCacheTest, truncate_to_no_clusters_forgets_every_extent)
{
    extents.Add(fs::FatExtent{ 0, 10, 2 });

    extents.Truncate(0);

    EXPECT_EQ(std::nullopt, extents.Last());
}

TEST_F(FatExtentCacheTest, truncate_keeps_room_for_new_extents)
{
    extents.Add(fs::FatExtent{ 0, 10, 1 });
    extents.Add(fs::FatExtent{ 2, 30, 1 });
    extents.Add(fs::FatExtent{ 4, 50, 1 });
    extents.Truncate(1);

    extents.Add(fs::FatExtent{ 1, 70, 1 });
    extents.Add(fs::FatExtent{ 3, 90, 1 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 1 }), extents.Covering(0));
    EXPECT_EQ((fs::FatExtent{ 1, 70, 1 }), extents.Covering(1));
    EXPECT_EQ((fs::FatExtent{ 3, 90, 1 }), extents.Covering(3));
}

TEST(FatExtentCacheOfOneTest, single_extent_is_kept)
{
    fs::FatExtentCache::WithStorage<1> extents;
    extents.Add(fs::FatExtent{ 0, 10, 1 });

    extents.Add(fs::FatExtent{ 5, 50, 1 });

    EXPECT_EQ((fs::FatExtent{ 0, 10, 1 }), extents.Covering(0));
    EXPECT_EQ(std::nullopt, extents.Covering(5));
}
