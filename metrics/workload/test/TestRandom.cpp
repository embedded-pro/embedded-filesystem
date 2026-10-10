#include "metrics/workload/Random.hpp"
#include "gmock/gmock.h"
#include <array>

TEST(RandomTest, produces_the_splitmix64_reference_sequence)
{
    metrics::Random random{ 0 };

    EXPECT_EQ(0xe220a8397b1dcdaf, random.Next());
    EXPECT_EQ(0x6e789e6aa1b965f4, random.Next());
}

TEST(RandomTest, equal_seeds_give_equal_sequences)
{
    metrics::Random first{ 42 };
    metrics::Random second{ 42 };

    for (int draw = 0; draw != 100; ++draw)
        EXPECT_EQ(first.Next(), second.Next());
}

TEST(RandomTest, different_seeds_give_different_sequences)
{
    metrics::Random first{ 1 };
    metrics::Random second{ 2 };

    EXPECT_NE(first.Next(), second.Next());
}

TEST(RandomTest, below_stays_below_the_bound)
{
    metrics::Random random{ 7 };

    for (int draw = 0; draw != 1000; ++draw)
        EXPECT_LT(random.Below(10), 10);
}

TEST(RandomTest, below_one_is_always_zero)
{
    metrics::Random random{ 7 };

    for (int draw = 0; draw != 100; ++draw)
        EXPECT_EQ(0, random.Below(1));
}

TEST(RandomTest, below_reaches_every_value_of_a_small_bound)
{
    metrics::Random random{ 7 };
    std::array<bool, 4> seen{};

    for (int draw = 0; draw != 100; ++draw)
        seen[random.Below(4)] = true;

    EXPECT_EQ((std::array<bool, 4>{ true, true, true, true }), seen);
}
