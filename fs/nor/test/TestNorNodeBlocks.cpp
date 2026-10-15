#include "fs/nor/NorNodeBlocks.hpp"
#include "gtest/gtest.h"
#include <array>

namespace
{
    class NorNodeBlocksTest
        : public testing::Test
    {
    public:
        std::array<uint8_t, 2> storage{ 0xff, 0xff };
        fs::NorNodeBlocks nodeBlocks{ infra::MakeRange(storage) };
    };
}

TEST_F(NorNodeBlocksTest, needs_one_byte_for_every_eight_blocks)
{
    static_assert(fs::NorNodeBlockBytes(1) == 1);
    static_assert(fs::NorNodeBlockBytes(8) == 1);
    static_assert(fs::NorNodeBlockBytes(9) == 2);
    static_assert(fs::NorNodeBlockBytes(4096) == 512);
}

TEST_F(NorNodeBlocksTest, reset_holds_no_node_block)
{
    nodeBlocks.Reset(12);

    EXPECT_EQ(0, nodeBlocks.Count());
    for (uint32_t block = 0; block != 12; ++block)
        EXPECT_FALSE(nodeBlocks.Contains(block));
}

TEST_F(NorNodeBlocksTest, holds_the_added_blocks)
{
    nodeBlocks.Reset(12);

    nodeBlocks.Add(3);
    nodeBlocks.Add(8);
    nodeBlocks.Add(11);

    EXPECT_EQ(3, nodeBlocks.Count());
    EXPECT_FALSE(nodeBlocks.Contains(2));
    EXPECT_TRUE(nodeBlocks.Contains(3));
    EXPECT_FALSE(nodeBlocks.Contains(4));
    EXPECT_TRUE(nodeBlocks.Contains(8));
    EXPECT_TRUE(nodeBlocks.Contains(11));
}

TEST_F(NorNodeBlocksTest, adding_a_block_twice_counts_it_once)
{
    nodeBlocks.Reset(12);

    nodeBlocks.Add(5);
    nodeBlocks.Add(5);

    EXPECT_EQ(1, nodeBlocks.Count());
}

TEST_F(NorNodeBlocksTest, a_removed_block_is_no_node_block)
{
    nodeBlocks.Reset(12);
    nodeBlocks.Add(5);
    nodeBlocks.Add(6);

    nodeBlocks.Remove(5);
    nodeBlocks.Remove(7);

    EXPECT_EQ(1, nodeBlocks.Count());
    EXPECT_FALSE(nodeBlocks.Contains(5));
    EXPECT_TRUE(nodeBlocks.Contains(6));
}

TEST_F(NorNodeBlocksTest, reset_forgets_the_blocks)
{
    nodeBlocks.Reset(12);
    nodeBlocks.Add(5);

    nodeBlocks.Reset(10);

    EXPECT_EQ(0, nodeBlocks.Count());
    EXPECT_FALSE(nodeBlocks.Contains(5));
}

TEST_F(NorNodeBlocksTest, more_blocks_than_the_storage_holds_abort)
{
    EXPECT_DEATH(nodeBlocks.Reset(17), "");
}
