#include "fs/common/IdleTimer.hpp"
#include "fs/common/OperationQueue.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <chrono>

using namespace std::chrono_literals;

namespace
{
    class IdleTimerTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        void Grant(fs::OperationQueue::Claimer& claimer, int id)
        {
            claimer.Claim([this, id]
                {
                    granted.callback(id);
                });
        }

        void GrantBackground()
        {
            background.Claim([this]
                {
                    granted.callback(0);
                });
        }

        void ClaimAndGrantFirst()
        {
            Grant(first, 1);
            EXPECT_CALL(granted, callback(1));
            ExecuteAllActions();
        }

        testing::StrictMock<infra::MockCallback<void()>> idle;
        testing::StrictMock<infra::MockCallback<void(int)>> granted;
        fs::OperationQueue queue{ [this]
            {
                idleTimer.ForegroundClaiming();
            },
            [this]
            {
                idleTimer.ForegroundReleased();
            } };
        fs::OperationQueue::Claimer first{ queue };
        fs::OperationQueue::Claimer second{ queue };
        fs::OperationQueue::BackgroundClaimer background{ queue };
        fs::IdleTimer idleTimer{ background, 100ms, [this]
            {
                idle.callback();
            } };
    };
}

TEST_F(IdleTimerTest, no_idle_notification_without_operations)
{
    ForwardTime(1s);

    EXPECT_FALSE(idleTimer.Armed());
}

TEST_F(IdleTimerTest, is_armed_during_the_idle_delay_after_a_foreground_release)
{
    ClaimAndGrantFirst();
    first.Release();

    EXPECT_TRUE(idleTimer.Armed());
    EXPECT_CALL(idle, callback());
    ForwardTime(100ms);
    EXPECT_FALSE(idleTimer.Armed());
}

TEST_F(IdleTimerTest, a_foreground_claim_disarms_it)
{
    ClaimAndGrantFirst();
    first.Release();

    Grant(second, 2);

    EXPECT_FALSE(idleTimer.Armed());
    EXPECT_CALL(granted, callback(2));
    ExecuteAllActions();
}

TEST_F(IdleTimerTest, idle_notification_after_idle_delay_once_queue_is_empty)
{
    ClaimAndGrantFirst();

    first.Release();

    EXPECT_CALL(idle, callback()).With(After(100ms));
    ForwardTime(1s);
}

TEST_F(IdleTimerTest, no_idle_notification_while_claims_are_pending)
{
    ClaimAndGrantFirst();
    Grant(second, 2);

    first.Release();

    EXPECT_CALL(granted, callback(2));
    ForwardTime(1s);
}

TEST_F(IdleTimerTest, claim_during_idle_delay_cancels_idle_notification)
{
    ClaimAndGrantFirst();
    first.Release();
    ForwardTime(50ms);

    Grant(second, 2);

    EXPECT_CALL(granted, callback(2));
    ForwardTime(1s);
}

TEST_F(IdleTimerTest, idle_notification_is_given_once_per_idle_period)
{
    ClaimAndGrantFirst();
    first.Release();

    EXPECT_CALL(idle, callback());
    ForwardTime(1s);
    ForwardTime(1s);
}

TEST_F(IdleTimerTest, background_release_does_not_arm_idle_notification)
{
    GrantBackground();
    EXPECT_CALL(granted, callback(0));
    ExecuteAllActions();

    background.Release();

    ForwardTime(1s);
}

TEST_F(IdleTimerTest, foreground_release_after_background_yield_arms_idle_notification)
{
    GrantBackground();
    EXPECT_CALL(granted, callback(0));
    ExecuteAllActions();
    Grant(first, 1);
    background.Release();

    EXPECT_CALL(granted, callback(1)).WillOnce([this]
        {
            first.Release();
        });
    EXPECT_CALL(idle, callback());
    ForwardTime(1s);
}

TEST_F(IdleTimerTest, a_withdrawn_claim_does_not_keep_the_queue_busy)
{
    ClaimAndGrantFirst();
    Grant(second, 2);

    second.Release();
    first.Release();

    EXPECT_CALL(idle, callback());
    ForwardTime(1s);
}
