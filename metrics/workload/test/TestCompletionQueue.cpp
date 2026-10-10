#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "metrics/workload/CompletionQueue.hpp"
#include "gmock/gmock.h"
#include <vector>

namespace
{
    class CompletionQueueTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        metrics::CompletionQueue queue;
        std::vector<int> ran;
    };
}

TEST_F(CompletionQueueTest, a_deferred_action_runs_only_when_the_dispatcher_runs)
{
    queue.Defer([this]
        {
            ran.push_back(1);
        });

    EXPECT_TRUE(ran.empty());
    EXPECT_FALSE(queue.Empty());

    ExecuteAllActions();

    EXPECT_EQ((std::vector<int>{ 1 }), ran);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(CompletionQueueTest, deferred_actions_run_in_order)
{
    queue.Defer([this]
        {
            ran.push_back(1);
        });
    queue.Defer([this]
        {
            ran.push_back(2);
        });

    ExecuteAllActions();

    EXPECT_EQ((std::vector<int>{ 1, 2 }), ran);
}

TEST_F(CompletionQueueTest, an_action_may_defer_another_action)
{
    queue.Defer([this]
        {
            ran.push_back(1);
            queue.Defer([this]
                {
                    ran.push_back(2);
                });
        });

    ExecuteAllActions();

    EXPECT_EQ((std::vector<int>{ 1, 2 }), ran);
}
