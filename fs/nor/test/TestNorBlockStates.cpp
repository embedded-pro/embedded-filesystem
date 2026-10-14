#include "fs/nor/NorBlockStates.hpp"
#include "gtest/gtest.h"
#include <array>

namespace
{
    class NorBlockStatesTest
        : public testing::Test
    {
    public:
        std::array<uint8_t, 3> storage{ 0xff, 0xff, 0xff };
        fs::NorBlockStates states{ infra::MakeRange(storage) };
    };
}

TEST_F(NorBlockStatesTest, needs_one_byte_for_every_four_blocks)
{
    static_assert(fs::NorBlockStateBytes(1) == 1);
    static_assert(fs::NorBlockStateBytes(4) == 1);
    static_assert(fs::NorBlockStateBytes(5) == 2);
    static_assert(fs::NorBlockStateBytes(4096) == 1024);
}

TEST_F(NorBlockStatesTest, reset_marks_every_block_free_and_not_erased)
{
    states.Reset(10);

    EXPECT_EQ(10, states.Blocks());
    for (uint32_t block = 0; block != 10; ++block)
        EXPECT_EQ(fs::NorBlockState::freeNotErased, states.State(block));
}

TEST_F(NorBlockStatesTest, keeps_the_state_of_each_block)
{
    states.Reset(12);

    states.Set(4, fs::NorBlockState::freeErased);
    states.Set(5, fs::NorBlockState::inUse);
    states.Set(6, fs::NorBlockState::pending);
    states.Set(11, fs::NorBlockState::inUse);

    EXPECT_EQ(fs::NorBlockState::freeNotErased, states.State(3));
    EXPECT_EQ(fs::NorBlockState::freeErased, states.State(4));
    EXPECT_EQ(fs::NorBlockState::inUse, states.State(5));
    EXPECT_EQ(fs::NorBlockState::pending, states.State(6));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, states.State(7));
    EXPECT_EQ(fs::NorBlockState::inUse, states.State(11));
}

TEST_F(NorBlockStatesTest, setting_a_state_replaces_the_previous_one)
{
    states.Reset(4);

    states.Set(2, fs::NorBlockState::pending);
    states.Set(2, fs::NorBlockState::freeErased);

    EXPECT_EQ(fs::NorBlockState::freeErased, states.State(2));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, states.State(1));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, states.State(3));
}

TEST_F(NorBlockStatesTest, counts_the_blocks_in_a_state)
{
    states.Reset(9);
    states.Set(0, fs::NorBlockState::inUse);
    states.Set(1, fs::NorBlockState::inUse);
    states.Set(8, fs::NorBlockState::freeErased);

    EXPECT_EQ(2, states.Count(fs::NorBlockState::inUse));
    EXPECT_EQ(1, states.Count(fs::NorBlockState::freeErased));
    EXPECT_EQ(6, states.Count(fs::NorBlockState::freeNotErased));
    EXPECT_EQ(0, states.Count(fs::NorBlockState::pending));
}

TEST_F(NorBlockStatesTest, more_blocks_than_the_storage_holds_abort)
{
    EXPECT_DEATH(states.Reset(13), "");
}
