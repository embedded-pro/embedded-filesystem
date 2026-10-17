#include "infra/event/test_helper/EventDispatcherFixture.hpp"
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
