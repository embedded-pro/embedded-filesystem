#include "fs/fat/FatAllocationUnits.hpp"
#include "gtest/gtest.h"

namespace
{
    fs::FatGeometry Geometry(uint32_t firstDataSector, uint32_t sectorsPerCluster)
    {
        fs::FatGeometry geometry;
        geometry.firstDataSector = firstDataSector;
        geometry.sectorsPerCluster = sectorsPerCluster;
        return geometry;
    }
}

TEST(FatAllocationUnitsTest, aligned_data_area_starts_a_unit_at_the_first_cluster)
{
    EXPECT_EQ((fs::FatAllocationUnits{ 256, 2 }), fs::AllocationUnitsOf(Geometry(8192, 32), 8192));
}

TEST(FatAllocationUnitsTest, first_boundary_follows_an_unaligned_data_area)
{
    EXPECT_EQ((fs::FatAllocationUnits{ 4, 4 }), fs::AllocationUnitsOf(Geometry(1020, 2), 8));
}

TEST(FatAllocationUnitsTest, unknown_unit_is_not_used)
{
    EXPECT_EQ(std::nullopt, fs::AllocationUnitsOf(Geometry(8192, 32), 0));
}

TEST(FatAllocationUnitsTest, unit_that_is_not_a_whole_number_of_clusters_is_not_used)
{
    EXPECT_EQ(std::nullopt, fs::AllocationUnitsOf(Geometry(8192, 32), 16));
    EXPECT_EQ(std::nullopt, fs::AllocationUnitsOf(Geometry(8192, 3), 8));
}

TEST(FatAllocationUnitsTest, clusters_that_straddle_unit_boundaries_are_not_used)
{
    EXPECT_EQ(std::nullopt, fs::AllocationUnitsOf(Geometry(1017, 2), 8));
}

TEST(FatAllocationUnitsTest, boundaries_repeat_every_unit)
{
    fs::FatAllocationUnits units{ 4, 4 };

    EXPECT_FALSE(units.IsBoundary(2));
    EXPECT_TRUE(units.IsBoundary(4));
    EXPECT_FALSE(units.IsBoundary(6));
    EXPECT_TRUE(units.IsBoundary(8));
    EXPECT_EQ(4u, units.NextBoundaryAfter(2));
    EXPECT_EQ(8u, units.NextBoundaryAfter(4));
    EXPECT_EQ(8u, units.NextBoundaryAfter(7));
}
