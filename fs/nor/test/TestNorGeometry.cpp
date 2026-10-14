#include "fs/nor/NorGeometry.hpp"
#include "gtest/gtest.h"

namespace
{
    constexpr fs::NorConfig spiNor{ .maxBlocks = 40000 };
    constexpr fs::NorConfig internalFlash{ .maxBlocks = 256, .programUnit = 16, .pageSize = 16 };
    constexpr fs::NorConfig twoSectors{ .maxBlocks = 64, .sectorsPerBlock = 2 };
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
    EXPECT_EQ(8u, geometry->WatermarkBlocks());
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
    EXPECT_EQ(11u, geometry->WatermarkBlocks());
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

TEST(NorGeometryTest, a_volume_holds_the_superblocks_the_reserve_and_the_watermark)
{
    EXPECT_TRUE(fs::MakeNorGeometry(spiNor, 4096, 18).has_value());
    EXPECT_FALSE(fs::MakeNorGeometry(spiNor, 4096, 17).has_value());
}
