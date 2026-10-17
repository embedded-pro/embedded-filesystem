#include "validation/firmware/timing/Nanoseconds.hpp"
#include "validation/firmware/timing/Samples.hpp"
#include "gmock/gmock.h"
#include <cstdint>
#include <initializer_list>

namespace
{
    void AddAll(validation::Samples& samples, std::initializer_list<uint32_t> values)
    {
        for (auto value : values)
            samples.Add(value);
    }
}

TEST(SamplesTest, has_no_samples_when_cleared)
{
    validation::Samples::WithCapacity<4> samples;
    samples.Add(5);
    samples.Clear();
    samples.Add(7);

    EXPECT_EQ(1, samples.Size());
    EXPECT_EQ(7, samples.Mean());
}

TEST(SamplesTest, gives_the_minimum_the_median_the_maximum_and_the_mean)
{
    validation::Samples::WithCapacity<8> samples;
    AddAll(samples, { 30, 10, 50, 20, 40 });

    EXPECT_EQ(5, samples.Size());
    EXPECT_EQ(10, samples.Minimum());
    EXPECT_EQ(30, samples.Median());
    EXPECT_EQ(50, samples.Maximum());
    EXPECT_EQ(30, samples.Mean());
}

TEST(SamplesTest, the_median_of_an_even_count_is_the_lower_middle_value_as_on_the_host)
{
    validation::Samples::WithCapacity<8> samples;
    AddAll(samples, { 40, 10, 30, 20 });

    EXPECT_EQ(20, samples.Median());
}

TEST(SamplesTest, a_percentile_takes_the_nearest_rank_as_on_the_host)
{
    validation::Samples::WithCapacity<100> samples;
    for (uint32_t value = 100; value != 0; --value)
        samples.Add(value);

    EXPECT_EQ(99, samples.Percentile(99));
    EXPECT_EQ(50, samples.Percentile(50));
    EXPECT_EQ(100, samples.Percentile(100));
    EXPECT_EQ(1, samples.Percentile(1));
}

TEST(SamplesTest, the_99th_percentile_of_few_samples_is_the_maximum)
{
    validation::Samples::WithCapacity<8> samples;
    AddAll(samples, { 3, 1, 2 });

    EXPECT_EQ(3, samples.Percentile(99));
}

TEST(SamplesTest, the_mean_rounds_down_and_does_not_overflow)
{
    validation::Samples::WithCapacity<4> samples;
    AddAll(samples, { 0xffffffff, 0xffffffff, 0xfffffffe });

    EXPECT_EQ(0xfffffffe, samples.Mean());
}

TEST(SamplesTest, is_full_at_its_capacity)
{
    validation::Samples::WithCapacity<3> samples;
    AddAll(samples, { 2, 1 });
    EXPECT_FALSE(samples.Full());

    samples.Add(0);
    EXPECT_TRUE(samples.Full());
    EXPECT_EQ(2, samples.Maximum());
}

TEST(NanosecondsTest, converts_cycles_at_a_clock_to_nanoseconds)
{
    EXPECT_EQ(2500, validation::Nanoseconds(1000, 400'000'000));
    EXPECT_EQ(1'000'000'000'000, validation::Nanoseconds(400'000'000'000, 400'000'000));
}
