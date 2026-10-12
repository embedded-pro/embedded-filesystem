#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/Timer.hpp"
#include "metrics/device/VirtualTimeEnvironment.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>

using namespace std::chrono_literals;

TEST(VirtualTimeEnvironmentTest, now_starts_at_the_epoch)
{
    metrics::VirtualTimeEnvironment environment;

    EXPECT_EQ(infra::TimePoint(), environment.Time().Now());
    EXPECT_EQ(infra::TimePoint(), infra::Now());
}

TEST(VirtualTimeEnvironmentTest, timers_fire_when_time_runs)
{
    metrics::VirtualTimeEnvironment environment;
    bool fired = false;
    infra::TimerSingleShot timer{ 5ms, [&fired]
        {
            fired = true;
        } };

    environment.Time().RunFor(5ms);

    EXPECT_TRUE(fired);
}

TEST(VirtualTimeEnvironmentTest, completions_of_test_doubles_are_delivered_by_its_dispatcher)
{
    metrics::VirtualTimeEnvironment environment;
    hal::FlashStub flash{ 1, 16 };
    std::array<uint8_t, 2> data{ 1, 2 };
    bool done = false;

    flash.WriteBuffer(infra::MakeRange(data), 0, [&done]
        {
            done = true;
        });

    EXPECT_TRUE(environment.Time().RunUntil([&done]
        {
            return done;
        }));
}
