#include "fs/common/DeferredCompletions.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"

namespace
{
    class DeferredCompletionsTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Defer(int id)
        {
            completions.Defer([this, id]
                {
                    done.callback(id);
                });
        }

        testing::StrictMock<infra::MockCallback<void(int)>> done;
        fs::DeferredCompletions::WithMaxSize<2> completions;
    };
}

TEST_F(DeferredCompletionsTest, deferred_completion_runs_from_the_event_dispatcher)
{
    Defer(1);

    EXPECT_CALL(done, callback(1));
    ExecuteAllActions();
}

TEST_F(DeferredCompletionsTest, deferred_completions_run_in_order)
{
    Defer(1);
    Defer(2);

    testing::InSequence sequence;
    EXPECT_CALL(done, callback(1));
    EXPECT_CALL(done, callback(2));
    ExecuteAllActions();
}

TEST_F(DeferredCompletionsTest, completion_deferred_while_running_runs_in_a_later_round)
{
    Defer(1);

    EXPECT_CALL(done, callback(1)).WillOnce([this]
        {
            Defer(2);
        });
    ExecuteFirstAction();

    EXPECT_CALL(done, callback(2));
    ExecuteAllActions();
}

TEST_F(DeferredCompletionsTest, one_event_runs_every_pending_completion)
{
    Defer(1);
    Defer(2);

    EXPECT_CALL(done, callback(1));
    EXPECT_CALL(done, callback(2));
    ExecuteFirstAction();
    EXPECT_TRUE(IsIdle());
}

TEST_F(DeferredCompletionsTest, full_when_capacity_reached)
{
    EXPECT_FALSE(completions.Full());
    Defer(1);
    EXPECT_FALSE(completions.Full());
    Defer(2);
    EXPECT_TRUE(completions.Full());

    EXPECT_CALL(done, callback(1));
    EXPECT_CALL(done, callback(2));
    ExecuteAllActions();
    EXPECT_FALSE(completions.Full());
}

TEST_F(DeferredCompletionsTest, slot_is_free_while_its_completion_runs)
{
    fs::DeferredCompletions::WithMaxSize<1> single;
    single.Defer([this, &single]
        {
            done.callback(1);
            single.Defer([this]
                {
                    done.callback(2);
                });
        });

    testing::InSequence sequence;
    EXPECT_CALL(done, callback(1));
    EXPECT_CALL(done, callback(2));
    ExecuteAllActions();
}
