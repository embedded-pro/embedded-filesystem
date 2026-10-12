#include "infra/event/EventDispatcher.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/device/VirtualTime.hpp"
#include "gmock/gmock.h"
#include <chrono>

using namespace std::chrono_literals;

namespace
{
    class VirtualTimeTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        metrics::VirtualTime time{ systemTimerService };
        infra::TimePoint start{ systemTimerService.Now() };
        bool firstFired{ false };
        bool secondFired{ false };
        infra::TimerSingleShot first;
        infra::TimerSingleShot second;
    };
}

TEST_F(VirtualTimeTest, run_until_returns_immediately_when_the_predicate_holds)
{
    EXPECT_TRUE(time.RunUntil([]
        {
            return true;
        }));
    EXPECT_EQ(start, time.Now());
}

TEST_F(VirtualTimeTest, run_until_executes_scheduled_actions_before_checking_the_predicate)
{
    bool executed = false;
    infra::EventDispatcher::Instance().Schedule([&executed]
        {
            executed = true;
        });

    EXPECT_TRUE(time.RunUntil([&executed]
        {
            return executed;
        }));
    EXPECT_EQ(start, time.Now());
}

TEST_F(VirtualTimeTest, run_until_jumps_to_the_next_timer_trigger_when_the_dispatcher_is_idle)
{
    first.Start(5ms, [this]
        {
            firstFired = true;
        });

    EXPECT_TRUE(time.RunUntil([this]
        {
            return firstFired;
        }));
    EXPECT_EQ(start + 5ms, time.Now());
}

TEST_F(VirtualTimeTest, run_until_stops_at_the_trigger_that_satisfies_the_predicate)
{
    first.Start(5ms, [this]
        {
            firstFired = true;
        });
    second.Start(10ms, [this]
        {
            secondFired = true;
        });

    EXPECT_TRUE(time.RunUntil([this]
        {
            return firstFired;
        }));
    EXPECT_EQ(start + 5ms, time.Now());
    EXPECT_FALSE(secondFired);
}

TEST_F(VirtualTimeTest, run_until_returns_false_when_idle_without_timers)
{
    EXPECT_FALSE(time.RunUntil([]
        {
            return false;
        }));
    EXPECT_EQ(start, time.Now());
}

TEST_F(VirtualTimeTest, run_until_returns_false_at_the_limit_without_passing_it)
{
    first.Start(10ms, [this]
        {
            firstFired = true;
        });

    EXPECT_FALSE(time.RunUntil([this]
        {
            return firstFired;
        },
        4ms));
    EXPECT_EQ(start + 4ms, time.Now());
    EXPECT_FALSE(firstFired);
}

TEST_F(VirtualTimeTest, run_for_advances_exactly_the_duration_and_fires_timers_on_the_way)
{
    first.Start(3ms, [this]
        {
            firstFired = true;
        });

    time.RunFor(10ms);

    EXPECT_TRUE(firstFired);
    EXPECT_EQ(start + 10ms, time.Now());
}

TEST_F(VirtualTimeTest, run_until_idle_runs_every_pending_timer)
{
    first.Start(3ms, [this]
        {
            firstFired = true;
        });
    second.Start(7ms, [this]
        {
            secondFired = true;
        });

    EXPECT_TRUE(time.RunUntilIdle());
    EXPECT_TRUE(firstFired);
    EXPECT_TRUE(secondFired);
    EXPECT_EQ(start + 7ms, time.Now());
}

TEST_F(VirtualTimeTest, run_until_idle_returns_false_at_the_limit)
{
    first.Start(10ms, [this]
        {
            firstFired = true;
        });

    EXPECT_FALSE(time.RunUntilIdle(4ms));
    EXPECT_EQ(start + 4ms, time.Now());
}

TEST_F(VirtualTimeTest, nested_run_until_aborts)
{
    EXPECT_DEATH(
        {
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    time.RunUntil([]
                        {
                            return true;
                        });
                });
            time.RunUntil([]
                {
                    return false;
                });
        },
        "");
}
