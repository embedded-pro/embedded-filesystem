#include "fs/fat/FatFormatLayout.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "gtest/gtest.h"

namespace
{
    std::optional<fs::FatFormatLayout> Layout(uint32_t blocks, const fs::FatFormatConfig& config = {})
    {
        return fs::ComputeFormatLayout(blocks, 512, config);
    }

    fs::FatFormatConfig Aligned(uint32_t alignment)
    {
        fs::FatFormatConfig config;
        config.alignmentSectors = alignment;
        return config;
    }

    void ExpectConsistent(const fs::FatFormatLayout& layout, uint32_t blocks)
    {
        auto geometry = fs::GeometryOf(layout.parameters, layout.volumeStart, blocks - layout.volumeStart, 512);

        EXPECT_EQ(fs::Result::success, geometry.result);
        EXPECT_EQ(layout.type, geometry.geometry.type);
        EXPECT_EQ(layout.dataStart, geometry.geometry.firstDataSector);
        EXPECT_EQ(layout.clusterCount, geometry.geometry.clusterCount);
    }
}

class SdParametersTest
    : public testing::TestWithParam<std::pair<uint32_t, fs::SdFormatParameters>>
{};

TEST_P(SdParametersTest, parameters_follow_the_capacity)
{
    EXPECT_EQ(GetParam().second, fs::SdParametersFor(GetParam().first));
}

INSTANTIATE_TEST_SUITE_P(EveryCapacity, SdParametersTest,
    testing::Values(std::make_pair(4096u, fs::SdFormatParameters{ fs::FatType::fat12, 16, 16 }),
        std::make_pair(16384u, fs::SdFormatParameters{ fs::FatType::fat12, 16, 16 }),
        std::make_pair(16385u, fs::SdFormatParameters{ fs::FatType::fat12, 32, 32 }),
        std::make_pair(131072u, fs::SdFormatParameters{ fs::FatType::fat12, 32, 32 }),
        std::make_pair(131073u, fs::SdFormatParameters{ fs::FatType::fat16, 32, 32 }),
        std::make_pair(524288u, fs::SdFormatParameters{ fs::FatType::fat16, 32, 32 }),
        std::make_pair(2097152u, fs::SdFormatParameters{ fs::FatType::fat16, 32, 64 }),
        std::make_pair(4194304u, fs::SdFormatParameters{ fs::FatType::fat16, 64, 64 }),
        std::make_pair(8388608u, fs::SdFormatParameters{ fs::FatType::fat32, 64, 8192 }),
        std::make_pair(67108864u, fs::SdFormatParameters{ fs::FatType::fat32, 64, 8192 }),
        std::make_pair(134217728u, fs::SdFormatParameters{ fs::FatType::fat32, 64, 32768 })));

TEST(FatFormatLayoutTest, small_card_is_fat12_with_the_system_area_ending_on_a_boundary_unit)
{
    auto layout = Layout(4096);

    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(fs::FatType::fat12, layout->type);
    EXPECT_EQ((fs::PartitionEntry{ 0x01, 13, 4083 }), layout->partition);
    EXPECT_EQ(13u, layout->volumeStart);
    EXPECT_EQ(48u, layout->dataStart);
    EXPECT_EQ(253u, layout->clusterCount);
    EXPECT_EQ(1, layout->parameters.reservedSectors);
    EXPECT_EQ(1u, layout->parameters.sectorsPerFat);
    EXPECT_EQ(512, layout->parameters.rootEntries);
    EXPECT_EQ(2, layout->parameters.fatCopies);
    EXPECT_EQ(0xf8, layout->parameters.media);
    EXPECT_EQ(13u, layout->parameters.hiddenSectors);
    EXPECT_EQ(4083u, layout->parameters.totalSectors);
    ExpectConsistent(*layout, 4096);
}

TEST(FatFormatLayoutTest, fat16_card_aligned_to_its_boundary_unit)
{
    auto layout = Layout(524288);

    EXPECT_EQ(fs::FatType::fat16, layout->type);
    EXPECT_EQ(29u, layout->volumeStart);
    EXPECT_EQ(192u, layout->dataStart);
    EXPECT_EQ(16378u, layout->clusterCount);
    EXPECT_EQ(0x06, layout->partition->type);
    ExpectConsistent(*layout, 524288);
}

TEST(FatFormatLayoutTest, allocation_unit_replaces_the_boundary_unit)
{
    auto layout = Layout(524288, Aligned(8192));

    EXPECT_EQ(fs::FatType::fat16, layout->type);
    EXPECT_EQ(8029u, layout->volumeStart);
    EXPECT_EQ(8192u, layout->dataStart);
    EXPECT_EQ(16128u, layout->clusterCount);
    EXPECT_EQ(65u, layout->parameters.sectorsPerFat);
    ExpectConsistent(*layout, 524288);
}

TEST(FatFormatLayoutTest, fat32_card_starts_its_partition_on_a_boundary_unit)
{
    auto layout = Layout(8388608);

    EXPECT_EQ(fs::FatType::fat32, layout->type);
    EXPECT_EQ((fs::PartitionEntry{ 0x0c, 8192, 8380416 }), layout->partition);
    EXPECT_EQ(16384u, layout->dataStart);
    EXPECT_EQ(6142, layout->parameters.reservedSectors);
    EXPECT_EQ(130816u, layout->clusterCount);
    EXPECT_EQ(0, layout->parameters.rootEntries);
    EXPECT_EQ(2u, layout->parameters.rootCluster);
    EXPECT_EQ(1, layout->parameters.fsInfoSector);
    EXPECT_EQ(6, layout->parameters.backupBootSector);
    EXPECT_TRUE(layout->parameters.fat32Layout);
    ExpectConsistent(*layout, 8388608);
}

TEST(FatFormatLayoutTest, volume_without_a_partition_table_absorbs_the_alignment_in_reserved_sectors)
{
    auto config = Aligned(8192);
    config.partitioned = false;

    auto layout = Layout(524288, config);

    EXPECT_EQ(std::nullopt, layout->partition);
    EXPECT_EQ(0u, layout->volumeStart);
    EXPECT_EQ(8030, layout->parameters.reservedSectors);
    EXPECT_EQ(8192u, layout->dataStart);
    EXPECT_EQ(524288u, layout->parameters.totalSectors);
    ExpectConsistent(*layout, 524288);
}

TEST(FatFormatLayoutTest, fat32_without_a_partition_table)
{
    fs::FatFormatConfig config;
    config.partitioned = false;

    auto layout = Layout(8388608, config);

    EXPECT_EQ(fs::FatType::fat32, layout->type);
    EXPECT_EQ(0u, layout->volumeStart);
    EXPECT_EQ(0u, layout->dataStart % 8192);
    ExpectConsistent(*layout, 8388608);
}

TEST(FatFormatLayoutTest, cluster_count_decides_the_type)
{
    auto layout = Layout(131072);

    EXPECT_EQ(fs::FatType::fat16, layout->type);
    EXPECT_EQ(4093u, layout->clusterCount);
    EXPECT_EQ(96u, layout->dataStart);
    ExpectConsistent(*layout, 131072);
}

TEST(FatFormatLayoutTest, small_clusters_make_fat32)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;

    auto layout = Layout(524288, config);

    EXPECT_EQ(fs::FatType::fat32, layout->type);
    EXPECT_EQ(32u, layout->volumeStart);
    EXPECT_EQ(62, layout->parameters.reservedSectors);
    EXPECT_EQ(8288u, layout->dataStart);
    EXPECT_EQ(516000u, layout->clusterCount);
    ExpectConsistent(*layout, 524288);
}

TEST(FatFormatLayoutTest, one_fat_copy)
{
    fs::FatFormatConfig config;
    config.fatCopies = 1;

    auto layout = Layout(4096, config);

    EXPECT_EQ(1, layout->parameters.fatCopies);
    EXPECT_EQ(14u, layout->volumeStart);
    EXPECT_EQ(48u, layout->dataStart);
    ExpectConsistent(*layout, 4096);
}

TEST(FatFormatLayoutTest, volume_id_and_root_entries_are_configured)
{
    fs::FatFormatConfig config;
    config.volumeId = 0x12345678;
    config.rootEntries = 256;

    auto layout = Layout(4096, config);

    EXPECT_EQ(0x12345678u, layout->parameters.volumeId);
    EXPECT_EQ(256, layout->parameters.rootEntries);
    ExpectConsistent(*layout, 4096);
}

TEST(FatFormatLayoutTest, partition_type_of_a_small_fat16_volume)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 4;

    auto layout = Layout(32768, config);

    EXPECT_EQ(fs::FatType::fat16, layout->type);
    EXPECT_EQ(0x04, layout->partition->type);
}

TEST(FatFormatLayoutTest, system_area_that_ends_on_a_boundary_unit_starts_one_unit_in)
{
    fs::FatFormatConfig config;
    config.rootEntries = 208;

    auto layout = Layout(4096, config);

    EXPECT_EQ(16u, layout->volumeStart);
    EXPECT_EQ(32u, layout->dataStart);
    ExpectConsistent(*layout, 4096);
}

TEST(FatFormatLayoutTest, size_where_neither_type_fits_its_cluster_count_is_rejected)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;

    EXPECT_EQ(std::nullopt, Layout(4149, config));
    EXPECT_NE(std::nullopt, Layout(4148, config));
}

TEST(FatFormatLayoutTest, device_too_small_for_one_cluster_is_rejected)
{
    EXPECT_EQ(std::nullopt, Layout(48));
}

TEST(FatFormatLayoutTest, invalid_configurations_are_rejected)
{
    EXPECT_EQ(std::nullopt, fs::ComputeFormatLayout(4096, 4096, {}));

    fs::FatFormatConfig config;
    config.fatCopies = 0;
    EXPECT_EQ(std::nullopt, Layout(4096, config));

    config = {};
    config.sectorsPerCluster = 3;
    EXPECT_EQ(std::nullopt, Layout(4096, config));

    config = {};
    config.alignmentSectors = 0;
    EXPECT_EQ(std::nullopt, Layout(4096, config));
}
