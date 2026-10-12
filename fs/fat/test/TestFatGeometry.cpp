#include "fs/fat/FatGeometry.hpp"
#include "gtest/gtest.h"

namespace
{
    const fs::BiosParameterBlock fat12Parameters{ 512, 4, 1, 2, 512, 4096, 0xf8, 3, 0, false, 0, 0, 0, 0, 0, 0 };
    const fs::BiosParameterBlock fat16Parameters{ 512, 1, 1, 2, 512, 16384, 0xf8, 64, 0, false, 0, 0, 0, 0, 0, 0 };
    const fs::BiosParameterBlock fat32Parameters{ 512, 1, 32, 2, 0, 81920, 0xf8, 630, 0, true, 0, 0, 2, 1, 6, 0 };

    fs::FatGeometryResult Geometry(const fs::BiosParameterBlock& parameters, uint32_t volumeStart = 0)
    {
        return fs::GeometryOf(parameters, volumeStart, parameters.totalSectors, 512);
    }

    fs::Result ResultOf(const fs::BiosParameterBlock& parameters)
    {
        return Geometry(parameters).result;
    }
}

TEST(FatGeometryTest, type_follows_the_cluster_count)
{
    EXPECT_EQ(fs::FatType::fat12, fs::TypeOfClusterCount(1));
    EXPECT_EQ(fs::FatType::fat12, fs::TypeOfClusterCount(4084));
    EXPECT_EQ(fs::FatType::fat16, fs::TypeOfClusterCount(4085));
    EXPECT_EQ(fs::FatType::fat16, fs::TypeOfClusterCount(65524));
    EXPECT_EQ(fs::FatType::fat32, fs::TypeOfClusterCount(65525));
}

TEST(FatGeometryTest, geometry_of_fat12)
{
    auto geometry = Geometry(fat12Parameters);

    EXPECT_EQ(fs::Result::success, geometry.result);
    EXPECT_EQ(fs::FatType::fat12, geometry.geometry.type);
    EXPECT_EQ(1u, geometry.geometry.firstFatSector);
    EXPECT_EQ(7u, geometry.geometry.rootSector);
    EXPECT_EQ(32u, geometry.geometry.rootSectors);
    EXPECT_EQ(39u, geometry.geometry.firstDataSector);
    EXPECT_EQ(1014u, geometry.geometry.clusterCount);
    EXPECT_EQ(std::nullopt, geometry.geometry.fsInfoSector);
}

TEST(FatGeometryTest, geometry_of_fat16)
{
    auto geometry = Geometry(fat16Parameters).geometry;

    EXPECT_EQ(fs::FatType::fat16, geometry.type);
    EXPECT_EQ(1u, geometry.sectorsPerCluster);
    EXPECT_EQ(64u, geometry.sectorsPerFat);
    EXPECT_EQ(2u, geometry.fatCopies);
    EXPECT_EQ(129u, geometry.rootSector);
    EXPECT_EQ(161u, geometry.firstDataSector);
    EXPECT_EQ(16223u, geometry.clusterCount);
    EXPECT_TRUE(geometry.mirrored);
}

TEST(FatGeometryTest, geometry_of_fat32)
{
    auto geometry = Geometry(fat32Parameters).geometry;

    EXPECT_EQ(fs::FatType::fat32, geometry.type);
    EXPECT_EQ(32u, geometry.firstFatSector);
    EXPECT_EQ(0u, geometry.rootSectors);
    EXPECT_EQ(2u, geometry.rootCluster);
    EXPECT_EQ(1292u, geometry.firstDataSector);
    EXPECT_EQ(80628u, geometry.clusterCount);
    EXPECT_EQ(1u, geometry.fsInfoSector);
    EXPECT_TRUE(geometry.mirrored);
    EXPECT_EQ(0, geometry.activeFat);
}

TEST(FatGeometryTest, sectors_are_offset_by_the_start_of_the_volume)
{
    auto geometry = Geometry(fat32Parameters, 8192).geometry;

    EXPECT_EQ(8224u, geometry.firstFatSector);
    EXPECT_EQ(9484u, geometry.firstDataSector);
    EXPECT_EQ(8193u, geometry.fsInfoSector);
}

TEST(FatGeometryTest, sector_of_cluster)
{
    auto geometry = Geometry(fat12Parameters).geometry;

    EXPECT_EQ(39u, geometry.SectorOfCluster(2));
    EXPECT_EQ(51u, geometry.SectorOfCluster(5));
}

TEST(FatGeometryTest, clusters_are_numbered_from_two)
{
    auto geometry = Geometry(fat12Parameters).geometry;

    EXPECT_FALSE(geometry.IsCluster(0));
    EXPECT_FALSE(geometry.IsCluster(1));
    EXPECT_TRUE(geometry.IsCluster(2));
    EXPECT_TRUE(geometry.IsCluster(1015));
    EXPECT_FALSE(geometry.IsCluster(1016));
}

TEST(FatGeometryTest, active_fat_is_the_first_while_mirrored)
{
    auto geometry = Geometry(fat16Parameters).geometry;

    EXPECT_EQ(1u, geometry.ActiveFatSector());
}

TEST(FatGeometryTest, mirroring_disabled_uses_the_active_fat_only)
{
    auto parameters = fat32Parameters;
    parameters.extendedFlags = 0x81;

    auto geometry = Geometry(parameters).geometry;

    EXPECT_FALSE(geometry.mirrored);
    EXPECT_EQ(1, geometry.activeFat);
    EXPECT_EQ(662u, geometry.ActiveFatSector());
}

TEST(FatGeometryTest, active_fat_beyond_the_copies_is_corrupted)
{
    auto parameters = fat32Parameters;
    parameters.extendedFlags = 0x82;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, active_fat_number_is_ignored_while_mirrored)
{
    auto parameters = fat32Parameters;
    parameters.extendedFlags = 0x05;

    EXPECT_EQ(0, Geometry(parameters).geometry.activeFat);
}

TEST(FatGeometryTest, fsinfo_outside_the_reserved_sectors_is_absent)
{
    auto parameters = fat32Parameters;

    parameters.fsInfoSector = 0;
    EXPECT_EQ(std::nullopt, Geometry(parameters).geometry.fsInfoSector);

    parameters.fsInfoSector = 32;
    EXPECT_EQ(std::nullopt, Geometry(parameters).geometry.fsInfoSector);
}

TEST(FatGeometryTest, sector_size_other_than_the_block_size_is_unsupported)
{
    EXPECT_EQ(fs::Result::unsupported, fs::GeometryOf(fat16Parameters, 0, 16384, 4096).result);
}

TEST(FatGeometryTest, zero_total_sectors_is_corrupted)
{
    auto parameters = fat16Parameters;
    parameters.totalSectors = 0;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, volume_beyond_its_device_is_corrupted)
{
    EXPECT_EQ(fs::Result::corrupted, fs::GeometryOf(fat16Parameters, 0, 16383, 512).result);
}

TEST(FatGeometryTest, zero_sectors_per_fat_is_corrupted)
{
    auto parameters = fat32Parameters;
    parameters.sectorsPerFat = 0;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, volume_without_room_for_one_cluster_is_corrupted)
{
    auto parameters = fat16Parameters;
    parameters.totalSectors = 161;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, fat_too_small_for_its_clusters_is_corrupted)
{
    auto parameters = fat16Parameters;
    parameters.sectorsPerFat = 63;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, fat12_fat_just_large_enough_is_valid)
{
    auto parameters = fat12Parameters;
    parameters.sectorsPerFat = 3;

    EXPECT_EQ(fs::Result::success, ResultOf(parameters));
}

TEST(FatGeometryTest, fat32_with_root_entries_is_corrupted)
{
    auto parameters = fat32Parameters;
    parameters.rootEntries = 512;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, fat32_clusters_with_a_fat16_layout_are_corrupted)
{
    auto parameters = fat32Parameters;
    parameters.fat32Layout = false;
    parameters.rootEntries = 0;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, fat16_clusters_with_a_fat32_layout_are_corrupted)
{
    auto parameters = fat16Parameters;
    parameters.fat32Layout = true;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, fat16_without_root_entries_is_corrupted)
{
    auto parameters = fat16Parameters;
    parameters.rootEntries = 0;

    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}

TEST(FatGeometryTest, other_fat32_version_is_unsupported)
{
    auto parameters = fat32Parameters;
    parameters.fileSystemVersion = 0x0100;

    EXPECT_EQ(fs::Result::unsupported, ResultOf(parameters));
}

TEST(FatGeometryTest, root_cluster_outside_the_clusters_is_corrupted)
{
    auto parameters = fat32Parameters;

    parameters.rootCluster = 1;
    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));

    parameters.rootCluster = 80630;
    EXPECT_EQ(fs::Result::corrupted, ResultOf(parameters));
}
