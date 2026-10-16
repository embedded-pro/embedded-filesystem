#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "validation/firmware/cycles/CycleSamples.hpp"
#include "validation/firmware/cycles/IdleCycleCounter.hpp"
#include "gmock/gmock.h"
#include <cstdint>

namespace
{
    class FakeCycleCounter
        : public validation::CycleCounter
    {
    public:
        uint32_t Cycles() const override
        {
            return cycles;
        }

        uint32_t cycles{ 0 };
    };

    class LowPowerStrategyMock
        : public infra::LowPowerStrategy
    {
    public:
        MOCK_METHOD(void, RequestExecution, (), (override));
        MOCK_METHOD(void, Idle, (const infra::EventDispatcherWorker& eventDispatcher), (override));
    };

    class IdleCycleCounterTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void ExpectIdleFor(uint32_t cycles)
        {
            EXPECT_CALL(strategy, Idle(testing::Ref(*this))).WillOnce([this, cycles](const infra::EventDispatcherWorker&)
                {
                    counter.cycles += cycles;
                });
            idle.Idle(*this);
        }

        FakeCycleCounter counter;
        testing::StrictMock<LowPowerStrategyMock> strategy;
        validation::IdleCycleCounter idle{ strategy, counter };
    };
}

TEST_F(IdleCycleCounterTest, counts_no_cycles_before_the_first_idle_period)
{
    EXPECT_EQ(0, idle.IdleCycles());
}

TEST_F(IdleCycleCounterTest, adds_the_cycles_spent_in_each_idle_period_of_the_strategy)
{
    counter.cycles = 1000;
    ExpectIdleFor(250);
    counter.cycles += 5000;
    ExpectIdleFor(40);

    EXPECT_EQ(290, idle.IdleCycles());
}

TEST_F(IdleCycleCounterTest, counts_an_idle_period_across_the_wrap_of_the_counter)
{
    counter.cycles = 0xffffff00;
    ExpectIdleFor(0x200);

    EXPECT_EQ(0x200, idle.IdleCycles());
}

TEST_F(IdleCycleCounterTest, forwards_requests_for_execution)
{
    EXPECT_CALL(strategy, RequestExecution());
    idle.RequestExecution();
}

TEST(CycleSamplesTest, has_no_samples_when_cleared)
{
    validation::CycleSamples samples;
    samples.Add(5);
    samples.Clear();

    EXPECT_EQ(0, samples.Size());
}

TEST(CycleSamplesTest, gives_the_minimum_the_median_and_the_maximum)
{
    validation::CycleSamples samples;
    for (uint32_t value : { 30, 10, 50, 20, 40 })
        samples.Add(value);

    EXPECT_EQ(5, samples.Size());
    EXPECT_EQ(10, samples.Minimum());
    EXPECT_EQ(30, samples.Median());
    EXPECT_EQ(50, samples.Maximum());
}

TEST(CycleSamplesTest, the_median_of_an_even_count_is_the_lower_middle_value_as_on_the_host)
{
    validation::CycleSamples samples;
    for (uint32_t value : { 40, 10, 30, 20 })
        samples.Add(value);

    EXPECT_EQ(20, samples.Median());
}

TEST(CycleSamplesTest, the_median_leaves_the_order_of_the_samples_alone)
{
    validation::CycleSamples samples;
    for (uint32_t value : { 3, 1, 2 })
        samples.Add(value);

    EXPECT_EQ(2, samples.Median());
    EXPECT_EQ(1, samples.Minimum());
    EXPECT_EQ(3, samples.Maximum());
    EXPECT_EQ(2, samples.Median());
}

TEST(CycleSamplesTest, holds_up_to_its_capacity)
{
    validation::CycleSamples samples;
    for (std::size_t index = 0; index != validation::CycleSamples::capacity; ++index)
        samples.Add(static_cast<uint32_t>(index));

    EXPECT_EQ(validation::CycleSamples::capacity, samples.Size());
    EXPECT_EQ(validation::CycleSamples::capacity - 1, samples.Maximum());
}
