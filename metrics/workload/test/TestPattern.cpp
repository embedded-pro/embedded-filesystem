#include "metrics/workload/Pattern.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    std::array<uint8_t, 64> Filled(uint32_t fileId, uint32_t offset)
    {
        std::array<uint8_t, 64> data{};
        metrics::FillPattern(infra::MakeRange(data), fileId, offset);
        return data;
    }
}

TEST(PatternTest, filled_data_matches_its_file_and_offset)
{
    auto data = Filled(3, 100);

    EXPECT_TRUE(metrics::MatchesPattern(infra::MakeRange(data), 3, 100));
}

TEST(PatternTest, filled_data_does_not_match_another_file)
{
    auto data = Filled(3, 100);

    EXPECT_FALSE(metrics::MatchesPattern(infra::MakeRange(data), 4, 100));
}

TEST(PatternTest, filled_data_does_not_match_another_offset)
{
    auto data = Filled(3, 100);

    EXPECT_FALSE(metrics::MatchesPattern(infra::MakeRange(data), 3, 101));
}

TEST(PatternTest, pattern_continues_across_chunks)
{
    auto whole = Filled(3, 0);
    std::array<uint8_t, 64> chunked{};

    metrics::FillPattern(infra::Head(infra::MakeRange(chunked), 20), 3, 0);
    metrics::FillPattern(infra::DiscardHead(infra::MakeRange(chunked), 20), 3, 20);

    EXPECT_EQ(whole, chunked);
}

TEST(PatternTest, a_changed_byte_does_not_match)
{
    auto data = Filled(3, 0);
    data[40] ^= 1;

    EXPECT_FALSE(metrics::MatchesPattern(infra::MakeRange(data), 3, 0));
}

TEST(PatternTest, erased_and_programmed_bytes_are_not_the_pattern)
{
    std::array<uint8_t, 64> erased{};
    erased.fill(0xff);
    std::array<uint8_t, 64> programmed{};

    EXPECT_FALSE(metrics::MatchesPattern(infra::MakeRange(erased), 0, 0));
    EXPECT_FALSE(metrics::MatchesPattern(infra::MakeRange(programmed), 0, 0));
}
