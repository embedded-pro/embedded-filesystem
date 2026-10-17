#include "infra/util/BoundedVector.hpp"
#include "validation/firmware/timing/LatencyHistogram.hpp"
#include "gmock/gmock.h"
#include <cstdint>
#include <limits>

TEST(LatencyHistogramTest, durations_below_four_nanoseconds_have_a_bucket_each)
{
    EXPECT_EQ(0, validation::LatencyHistogram::Bucket(0));
    EXPECT_EQ(1, validation::LatencyHistogram::Bucket(1));
    EXPECT_EQ(3, validation::LatencyHistogram::Bucket(3));
}

TEST(LatencyHistogramTest, each_octave_has_four_buckets)
{
    EXPECT_EQ(4, validation::LatencyHistogram::Bucket(4));
    EXPECT_EQ(7, validation::LatencyHistogram::Bucket(7));
    EXPECT_EQ(8, validation::LatencyHistogram::Bucket(8));
    EXPECT_EQ(8, validation::LatencyHistogram::Bucket(9));
    EXPECT_EQ(9, validation::LatencyHistogram::Bucket(10));
    EXPECT_EQ(11, validation::LatencyHistogram::Bucket(15));
    EXPECT_EQ(12, validation::LatencyHistogram::Bucket(16));
    EXPECT_EQ(35, validation::LatencyHistogram::Bucket(1'000));
    EXPECT_EQ(75, validation::LatencyHistogram::Bucket(1'000'000));
}

TEST(LatencyHistogramTest, durations_from_two_to_the_41_nanoseconds_share_the_last_octave)
{
    EXPECT_EQ(160, validation::LatencyHistogram::Bucket(uint64_t{ 1 } << 41));
    EXPECT_EQ(163, validation::LatencyHistogram::Bucket((uint64_t{ 1 } << 42) - 1));
    EXPECT_EQ(163, validation::LatencyHistogram::Bucket(uint64_t{ 1 } << 50));
    EXPECT_EQ(163, validation::LatencyHistogram::Bucket(std::numeric_limits<uint64_t>::max()));
    EXPECT_EQ(164, validation::LatencyHistogram::buckets);
}

TEST(LatencyHistogramTest, counts_the_durations_of_each_bucket)
{
    validation::LatencyHistogram histogram;

    histogram.Add(9);
    histogram.Add(8);
    histogram.Add(1'000);

    EXPECT_EQ(2, histogram.Count(8));
    EXPECT_EQ(1, histogram.Count(35));
    EXPECT_EQ(0, histogram.Count(9));
}

TEST(LatencyHistogramTest, lists_the_non_empty_buckets_as_pairs_of_bucket_and_count)
{
    validation::LatencyHistogram histogram;
    histogram.Add(1'000);
    histogram.Add(2);
    histogram.Add(1'000);
    infra::BoundedVector<uint32_t>::WithMaxSize<2 * validation::LatencyHistogram::buckets> pairs;

    histogram.NonEmpty(pairs);

    EXPECT_THAT(pairs, testing::ElementsAre(2, 1, 35, 2));
}

TEST(LatencyHistogramTest, is_empty_when_cleared)
{
    validation::LatencyHistogram histogram;
    histogram.Add(1'000);
    infra::BoundedVector<uint32_t>::WithMaxSize<2 * validation::LatencyHistogram::buckets> pairs;

    histogram.Clear();
    histogram.NonEmpty(pairs);

    EXPECT_TRUE(pairs.empty());
}
