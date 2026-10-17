#include "fs/common/TransferAlignment.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>

namespace
{
    class TransferAlignmentTest
        : public testing::Test
    {
    public:
        infra::ConstByteRange At(std::size_t offset) const
        {
            return infra::DiscardHead(infra::MakeRange(storage), offset);
        }

        alignas(fs::bufferAlignment) std::array<uint8_t, 2 * fs::bufferAlignment> storage{};
    };
}

TEST_F(TransferAlignmentTest, an_alignment_of_one_accepts_every_buffer)
{
    fs::TransferAlignment alignment{ 1 };

    EXPECT_TRUE(alignment.Accepts(At(0)));
    EXPECT_TRUE(alignment.Accepts(At(1)));
    EXPECT_TRUE(alignment.Accepts(At(3)));
}

TEST_F(TransferAlignmentTest, accepts_a_buffer_that_starts_at_a_multiple_of_the_alignment)
{
    fs::TransferAlignment alignment{ 4 };

    EXPECT_TRUE(alignment.Accepts(At(0)));
    EXPECT_TRUE(alignment.Accepts(At(4)));
    EXPECT_FALSE(alignment.Accepts(At(2)));
    EXPECT_FALSE(alignment.Accepts(At(5)));
}

TEST_F(TransferAlignmentTest, accepts_up_to_the_alignment_of_the_buffers_of_fs)
{
    fs::TransferAlignment alignment{ fs::bufferAlignment };

    EXPECT_TRUE(alignment.Accepts(At(0)));
    EXPECT_TRUE(alignment.Accepts(At(fs::bufferAlignment)));
    EXPECT_FALSE(alignment.Accepts(At(fs::bufferAlignment / 2)));
}

TEST_F(TransferAlignmentTest, the_alignment_is_a_power_of_two_of_at_most_the_alignment_of_the_buffers_of_fs)
{
    EXPECT_DEATH(fs::TransferAlignment{ 0 }, "");
    EXPECT_DEATH(fs::TransferAlignment{ 12 }, "");
    EXPECT_DEATH(fs::TransferAlignment{ 2 * fs::bufferAlignment }, "");
}
