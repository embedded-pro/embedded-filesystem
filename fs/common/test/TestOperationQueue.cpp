#include "fs/common/OperationQueue.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"

namespace
{
    class OperationQueueTest
        : public testing::Test
        , public infra::EventDispatcherFixture
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

        testing::StrictMock<infra::MockCallback<void(int)>> granted;
        fs::OperationQueue queue;
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

TEST_F(OperationQueueTest, releasing_a_queued_claim_withdraws_it)
{
    ClaimAndGrantFirst();
    Grant(second, 2);

    second.Release();
    first.Release();

    EXPECT_FALSE(second.IsQueued());
    EXPECT_FALSE(background.ShouldYield());
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

TEST_F(OperationQueueTest, notifies_each_release_of_a_granted_foreground_claim)
{
    testing::StrictMock<infra::MockCallback<void()>> released;
    fs::OperationQueue notifying{ nullptr, [&released]
        {
            released.callback();
        } };
    fs::OperationQueue::Claimer foreground{ notifying };
    fs::OperationQueue::BackgroundClaimer inBackground{ notifying };
    foreground.Claim([] {});
    ExecuteAllActions();

    EXPECT_CALL(released, callback());
    foreground.Release();
    testing::Mock::VerifyAndClearExpectations(&released);

    foreground.Release();
    inBackground.Claim([] {});
    ExecuteAllActions();
    inBackground.Release();
}

TEST_F(OperationQueueTest, an_urgent_background_claim_is_granted_ahead_of_waiting_claims)
{
    ClaimAndGrantFirst();
    Grant(second, 2);
    background.ClaimUrgent([this]
        {
            granted.callback(0);
        });

    first.Release();

    testing::InSequence sequence;
    EXPECT_CALL(granted, callback(0)).WillOnce([this]
        {
            background.Release();
        });
    EXPECT_CALL(granted, callback(2));
    ExecuteAllActions();
}

TEST_F(OperationQueueTest, releasing_a_queued_background_claim_withdraws_it)
{
    ClaimAndGrantFirst();
    GrantBackground();

    background.Release();
    first.Release();
    ExecuteAllActions();

    EXPECT_FALSE(background.IsClaimed());
    EXPECT_FALSE(background.ShouldYield());
}

TEST_F(OperationQueueTest, notifies_each_foreground_claim)
{
    testing::StrictMock<infra::MockCallback<void()>> claiming;
    fs::OperationQueue notifying{ [&claiming]
        {
            claiming.callback();
        } };
    fs::OperationQueue::Claimer foreground{ notifying };
    fs::OperationQueue::BackgroundClaimer inBackground{ notifying };
    fs::OperationQueue::BackgroundClaimer urgent{ notifying };

    EXPECT_CALL(claiming, callback());
    foreground.Claim([] {});
    testing::Mock::VerifyAndClearExpectations(&claiming);

    inBackground.Claim([] {});
    urgent.ClaimUrgent([] {});
    ExecuteAllActions();
    foreground.Release();
    urgent.Release();
    inBackground.Release();
}
