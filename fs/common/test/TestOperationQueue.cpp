#include "fs/common/OperationQueue.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <chrono>

using namespace std::chrono_literals;

namespace
{
    class OperationQueueTest
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
        fs::OperationQueue queue{ 100ms, [this]
            {
                idle.callback();
            } };
        fs::OperationQueue::Claimer first{ queue };
        fs::OperationQueue::Claimer second{ queue };
        fs::OperationQueue::Claimer third{ queue };
        fs::OperationQueue::BackgroundClaimer background{ queue };
    };
}

TEST_F(OperationQueueTest, claim_is_granted_from_the_event_dispatcher)
{
    Grant(first, 1);

    EXPECT_CALL(granted, callback(1));
    ExecuteAllActions();
    EXPECT_TRUE(first.IsClaimed());
}

TEST_F(OperationQueueTest, second_claim_waits_until_first_is_released)
{
    ClaimAndGrantFirst();

    Grant(second, 2);
    ExecuteAllActions();

    EXPECT_FALSE(second.IsClaimed());
    EXPECT_TRUE(second.IsQueued());
}

TEST_F(OperationQueueTest, claims_are_granted_in_order)
{
    Grant(first, 1);
    Grant(second, 2);
    Grant(third, 3);

    testing::InSequence sequence;
    EXPECT_CALL(granted, callback(1)).WillOnce([this]
        {
            first.Release();
        });
    EXPECT_CALL(granted, callback(2)).WillOnce([this]
        {
            second.Release();
        });
    EXPECT_CALL(granted, callback(3));
    ExecuteAllActions();
}

TEST_F(OperationQueueTest, release_grants_next_claim_from_the_event_dispatcher)
{
    ClaimAndGrantFirst();
    Grant(second, 2);

    first.Release();

    EXPECT_CALL(granted, callback(2));
    ExecuteAllActions();
}

TEST_F(OperationQueueTest, no_idle_notification_without_operations)
{
    ForwardTime(1s);
}

TEST_F(OperationQueueTest, idle_notification_after_idle_delay_once_queue_is_empty)
{
    ClaimAndGrantFirst();

    first.Release();

    EXPECT_CALL(idle, callback()).With(After(100ms));
    ForwardTime(1s);
}

TEST_F(OperationQueueTest, no_idle_notification_while_claims_are_pending)
{
    ClaimAndGrantFirst();
    Grant(second, 2);

    first.Release();

    EXPECT_CALL(granted, callback(2));
    ForwardTime(1s);
}

TEST_F(OperationQueueTest, claim_during_idle_delay_cancels_idle_notification)
{
    ClaimAndGrantFirst();
    first.Release();
    ForwardTime(50ms);

    Grant(second, 2);

    EXPECT_CALL(granted, callback(2));
    ForwardTime(1s);
}

TEST_F(OperationQueueTest, idle_notification_is_given_once_per_idle_period)
{
    ClaimAndGrantFirst();
    first.Release();

    EXPECT_CALL(idle, callback());
    ForwardTime(1s);
    ForwardTime(1s);
}

TEST_F(OperationQueueTest, background_claim_is_granted_in_order_with_foreground_claims)
{
    GrantBackground();
    Grant(first, 1);

    testing::InSequence sequence;
    EXPECT_CALL(granted, callback(0)).WillOnce([this]
        {
            background.Release();
        });
    EXPECT_CALL(granted, callback(1));
    ExecuteAllActions();
}

TEST_F(OperationQueueTest, background_should_yield_while_foreground_claim_waits)
{
    GrantBackground();
    EXPECT_CALL(granted, callback(0));
    ExecuteAllActions();
    EXPECT_TRUE(background.IsClaimed());
    EXPECT_FALSE(background.ShouldYield());

    Grant(first, 1);

    EXPECT_TRUE(background.ShouldYield());
}

TEST_F(OperationQueueTest, background_release_grants_waiting_foreground_claim)
{
    GrantBackground();
    EXPECT_CALL(granted, callback(0));
    ExecuteAllActions();
    Grant(first, 1);

    background.Release();

    EXPECT_CALL(granted, callback(1));
    ExecuteAllActions();
}

TEST_F(OperationQueueTest, background_release_does_not_arm_idle_notification)
{
    GrantBackground();
    EXPECT_CALL(granted, callback(0));
    ExecuteAllActions();

    background.Release();

    ForwardTime(1s);
}

TEST_F(OperationQueueTest, foreground_release_after_background_yield_arms_idle_notification)
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

TEST_F(OperationQueueTest, releasing_a_queued_claim_withdraws_it)
{
    ClaimAndGrantFirst();
    Grant(second, 2);

    second.Release();
    first.Release();

    EXPECT_FALSE(second.IsQueued());
    EXPECT_CALL(idle, callback());
    ForwardTime(1s);
}

TEST_F(OperationQueueTest, counts_the_releases_of_granted_foreground_claims)
{
    EXPECT_EQ(0, queue.ForegroundReleases());
    ClaimAndGrantFirst();
    first.Release();
    Grant(second, 2);
    second.Release();
    GrantBackground();
    EXPECT_CALL(granted, callback(0));
    ExecuteAllActions();
    background.Release();

    EXPECT_EQ(1, queue.ForegroundReleases());
}
