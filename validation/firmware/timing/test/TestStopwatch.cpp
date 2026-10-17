#include "infra/timer/test_helper/ClockFixture.hpp"
#include "validation/firmware/timing/Stopwatch.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "gmock/gmock.h"
#include <chrono>

class StopwatchTest
    : public testing::Test
    , public infra::ClockFixture
{
public:
    validation::FakeCycleCounter cycles;
    validation::Stopwatch stopwatch{ cycles, validation::FakeCycleCounter::clockHz };
};

TEST_F(StopwatchTest, measures_a_span_shorter_than_a_second_with_the_cycle_counter)
{
    stopwatch.Start();
    ForwardTime(std::chrono::milliseconds(999));
    cycles.added += 250;

    EXPECT_EQ(999'250'000, stopwatch.ElapsedNs());
}

TEST_F(StopwatchTest, measures_a_span_of_a_second_or_more_with_the_clock_since_the_cycle_counter_wraps)
{
    stopwatch.Start();
    ForwardTime(std::chrono::minutes(4));
    cycles.added += 250;

    EXPECT_EQ(240'000'000'000, stopwatch.ElapsedNs());
}

TEST_F(StopwatchTest, restarts_from_zero)
{
    stopwatch.Start();
    ForwardTime(std::chrono::seconds(2));
    stopwatch.Start();
    cycles.added += 7;

    EXPECT_EQ(7'000, stopwatch.ElapsedNs());
}
