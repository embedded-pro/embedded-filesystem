#include "fs/nor/NorGeometry.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "gtest/gtest.h"

namespace
{
    constexpr fs::NorConfig spiNor{ .maxBlocks = 40000 };
    constexpr fs::NorConfig internalFlash{ .maxBlocks = 256, .programUnit = 16, .pageSize = 16 };
    constexpr fs::NorConfig twoSectors{ .maxBlocks = 64, .sectorsPerBlock = 2 };

    template<class Change>
    bool HasGeometryAfter(Change change)
    {
        auto config = spiNor;
        change(config);
        return fs::MakeNorGeometry(config, 4096, 4096).has_value();
    }
}

TEST(NorGeometryTest, spi_nor_blocks_of_one_sector)
{
    auto geometry = fs::MakeNorGeometry(spiNor, 4096, 4096);

    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(4096u, geometry->blocks);
    EXPECT_EQ(4096u, geometry->BlockSize());
    EXPECT_EQ(36u, geometry->HeaderSize());
    EXPECT_EQ(12u, geometry->HeaderPartBOffset());
    EXPECT_EQ(4060u, geometry->PayloadSize());
    EXPECT_EQ(256u, geometry->NodeSlotsOffset());
    EXPECT_EQ(7u, geometry->NodesPerBlock());
    EXPECT_EQ(544u, geometry->CheckpointBytes());
    EXPECT_EQ(3u, geometry->NodeBlocksPerCheckpoint());
    EXPECT_EQ(8u, geometry->ReserveBlocks());
    EXPECT_EQ(11u, geometry->WatermarkBlocks());
    EXPECT_EQ(20480u, geometry->AddressOfBlock(5));
    EXPECT_EQ(5u, geometry->FirstSectorOfBlock(5));
    EXPECT_EQ(20u, geometry->RoundToProgramUnit(20));
}

TEST(NorGeometryTest, internal_flash_with_a_program_unit_of_16_bytes)
{
    auto geometry = fs::MakeNorGeometry(internalFlash, 2048, 256);

    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(48u, geometry->HeaderSize());
    EXPECT_EQ(16u, geometry->HeaderPartBOffset());
    EXPECT_EQ(2000u, geometry->PayloadSize());
    EXPECT_EQ(48u, geometry->NodeSlotsOffset());
    EXPECT_EQ(3u, geometry->NodesPerBlock());
    EXPECT_EQ(64u, geometry->CheckpointBytes());
    EXPECT_EQ(6u, geometry->NodeBlocksPerCheckpoint());
    EXPECT_EQ(14u, geometry->ReserveBlocks());
    EXPECT_EQ(17u, geometry->WatermarkBlocks());
    EXPECT_EQ(32u, geometry->RoundToProgramUnit(20));
}

TEST(NorGeometryTest, blocks_of_two_sectors_leave_a_last_odd_sector_unused)
{
    auto geometry = fs::MakeNorGeometry(twoSectors, 4096, 65);

    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(32u, geometry->blocks);
    EXPECT_EQ(8192u, geometry->BlockSize());
    EXPECT_EQ(15u, geometry->NodesPerBlock());
    EXPECT_EQ(24576u, geometry->AddressOfBlock(3));
    EXPECT_EQ(6u, geometry->FirstSectorOfBlock(3));
}

TEST(NorGeometryTest, more_blocks_than_the_configuration_allows_are_not_supported)
{
    EXPECT_TRUE(fs::MakeNorGeometry(internalFlash, 2048, 256).has_value());
    EXPECT_FALSE(fs::MakeNorGeometry(internalFlash, 2048, 257).has_value());
}

TEST(NorGeometryTest, a_sector_is_a_whole_number_of_pages)
{
    EXPECT_FALSE(fs::MakeNorGeometry(spiNor, 0, 4096).has_value());
    EXPECT_FALSE(fs::MakeNorGeometry(spiNor, 4000, 4096).has_value());
}

TEST(NorGeometryTest, a_block_holds_at_least_one_node)
{
    EXPECT_FALSE(fs::MakeNorGeometry(spiNor, 512, 4096).has_value());
}

TEST(NorGeometryTest, the_checkpoint_fits_one_block)
{
    EXPECT_TRUE(fs::MakeNorGeometry(spiNor, 4096, 32224).has_value());
    EXPECT_FALSE(fs::MakeNorGeometry(spiNor, 4096, 32225).has_value());
}

TEST(NorGeometryTest, a_device_spans_at_most_2_gib)
{
    EXPECT_TRUE(fs::MakeNorGeometry(spiNor, 65536, 32768).has_value());
    EXPECT_FALSE(fs::MakeNorGeometry(spiNor, 65536, 32769).has_value());
}

TEST(NorGeometryTest, a_volume_holds_the_superblocks_the_reserve_and_the_watermark)
{
    EXPECT_TRUE(fs::MakeNorGeometry(spiNor, 4096, 21).has_value());
    EXPECT_FALSE(fs::MakeNorGeometry(spiNor, 4096, 20).has_value());
}

TEST(NorGeometryTest, a_volume_needs_blocks_and_sectors)
{
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.maxBlocks = 3;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.sectorsPerBlock = 0;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.sectorsPerBlock = 0x10000;
        }));
}

TEST(NorGeometryTest, program_unit_and_page_are_powers_of_two_with_the_page_at_least_the_program_unit)
{
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.programUnit = 0;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.programUnit = 3;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.pageSize = 96;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.programUnit = 512;
        }));
}

TEST(NorGeometryTest, a_node_is_a_whole_number_of_pages_and_holds_two_inode_records)
{
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 640;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 256;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 0x10000;
        }));
    EXPECT_TRUE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 1024;
        }));
}

TEST(NorGeometryTest, the_cache_holds_at_least_the_largest_operation_and_a_checkpoint)
{
    EXPECT_TRUE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.cachedNodes = 15;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.cachedNodes = 14;
        }));
}

TEST(NorGeometryTest, the_inline_area_holds_at_least_the_block_header)
{
    EXPECT_TRUE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.inlineBytes = 36;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.inlineBytes = 35;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.programUnit = 16;
            config.pageSize = 16;
            config.inlineBytes = 47;
        }));
}

TEST(NorGeometryTest, names_are_1_to_255_bytes_and_shorter_than_a_path)
{
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.maxNameBytes = 0;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.maxNameBytes = 256;
            config.nodeSize = 2048;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.maxPathBytes = 64;
        }));
}

TEST(NorGeometryTest, an_epoch_spans_2_to_16_blocks)
{
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.journalBlocks = 1;
        }));
    EXPECT_TRUE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.journalBlocks = 16;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.journalBlocks = 17;
        }));
}

TEST(NorGeometryTest, wear_leveling_needs_a_threshold_that_relative_counts_can_hold_and_an_interval)
{
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.staticWearLevelingThreshold = 0;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.staticWearLevelingThreshold = 129;
        }));
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.staticWearLevelingInterval = 0;
        }));
}

TEST(NorGeometryTest, a_volume_has_a_slot_for_operations_on_paths)
{
    EXPECT_FALSE(HasGeometryAfter([](fs::NorConfig& config)
        {
            config.pathOperations = 0;
        }));
}

TEST(NorGeometryTest, takes_the_geometry_of_a_device_whose_sectors_have_one_size)
{
    hal::FlashStub flash{ 4096, 4096 };

    auto geometry = fs::MakeNorGeometry(spiNor, flash);

    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(4096, geometry->sectorSize);
    EXPECT_EQ(4096, geometry->blocks);
}

TEST(NorGeometryTest, a_device_with_sectors_of_several_sizes_or_none_has_no_geometry)
{
    hal::FlashStub uneven{ 4096, 4096 };
    uneven.sectors[7].resize(2048);
    hal::FlashStub empty{ 0, 4096 };

    EXPECT_EQ(std::nullopt, fs::MakeNorGeometry(spiNor, uneven));
    EXPECT_EQ(std::nullopt, fs::MakeNorGeometry(spiNor, empty));
}
