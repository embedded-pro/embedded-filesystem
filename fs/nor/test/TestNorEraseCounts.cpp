#include "fs/nor/NorEraseCounts.hpp"
#include "gtest/gtest.h"
#include <array>

namespace
{
    class NorEraseCountsTest
        : public testing::Test
    {
    public:
        NorEraseCountsTest()
        {
            counts.Reset(8);
        }

        std::array<uint8_t, 8> storage{};
        fs::NorEraseCounts counts{ infra::MakeRange(storage) };
    };
}

TEST_F(NorEraseCountsTest, keeps_counts_relative_to_a_base)
{
    counts.Record(2, 100);
    counts.Record(3, 120);

    EXPECT_EQ(100, counts.Count(2));
    EXPECT_EQ(120, counts.Count(3));
    EXPECT_LE(counts.Base(), 100);
}

TEST_F(NorEraseCountsTest, a_count_below_the_base_moves_the_base_down)
{
    counts.Record(2, 120);
    counts.Record(3, 40);

    EXPECT_EQ(120, counts.Count(2));
    EXPECT_EQ(40, counts.Count(3));
}

TEST_F(NorEraseCountsTest, moving_the_base_down_clamps_the_counts_that_no_longer_fit)
{
    counts.Record(2, 1000);
    counts.Record(3, 500);

    EXPECT_EQ(500, counts.Count(3));
    EXPECT_EQ(counts.Base() + 255, counts.Count(2));
}

TEST_F(NorEraseCountsTest, a_count_more_than_255_above_the_base_is_clamped)
{
    counts.Record(2, 10);
    counts.Record(3, 1000);

    EXPECT_EQ(10, counts.Count(2));
    EXPECT_EQ(counts.Base() + 255, counts.Count(3));
}

TEST_F(NorEraseCountsTest, normalize_moves_the_base_to_the_lowest_count_from_the_first_block)
{
    counts.Record(2, 300);
    counts.Record(3, 310);
    for (uint32_t block = 4; block != 8; ++block)
        counts.Record(block, 305);

    counts.Normalize(2);

    EXPECT_EQ(300, counts.Base());
    EXPECT_EQ(300, counts.Count(2));
    EXPECT_EQ(310, counts.Count(3));
    EXPECT_EQ(305, counts.Count(7));
}

TEST_F(NorEraseCountsTest, normalize_ignores_the_blocks_before_the_first)
{
    counts.Record(0, 5);
    for (uint32_t block = 1; block != 8; ++block)
        counts.Record(block, 50);

    counts.Normalize(1);

    EXPECT_EQ(50, counts.Base());
    EXPECT_EQ(50, counts.Count(1));
}

TEST_F(NorEraseCountsTest, counts_of_zero_keep_a_base_of_zero)
{
    for (uint32_t block = 0; block != 8; ++block)
        counts.Record(block, 0);

    counts.Normalize(0);

    EXPECT_EQ(0, counts.Base());
    EXPECT_EQ(0, counts.Count(5));
}

TEST_F(NorEraseCountsTest, reset_forgets_the_base)
{
    counts.Record(2, 1000);
    counts.Reset(8);
    counts.Record(2, 3);

    EXPECT_EQ(3, counts.Count(2));
}

TEST_F(NorEraseCountsTest, more_blocks_than_the_storage_holds_abort)
{
    EXPECT_DEATH(counts.Reset(9), "");
}

TEST_F(NorEraseCountsTest, normalize_from_beyond_the_last_block_keeps_the_base)
{
    counts.Record(2, 300);
    auto base = counts.Base();

    counts.Normalize(8);

    EXPECT_EQ(base, counts.Base());
    EXPECT_EQ(300, counts.Count(2));
}
