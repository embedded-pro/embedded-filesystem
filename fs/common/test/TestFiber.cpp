#include "fs/common/Fiber.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>

namespace
{
    class FiberTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Run(const infra::Function<void()>& newBody)
        {
            body = newBody;
            claimer.Claim([this]
                {
                    fiber.Run(claimer, body);
                });
            ExecuteAllActions();
        }

        testing::StrictMock<infra::MockCallback<void(int)>> step;
        infra::Function<void()> body;
        infra::ClaimableResource resource;
        infra::ClaimableResource::Claimer claimer{ resource };
        alignas(16) std::array<uint8_t, fs::Fiber::defaultStackBytes> stack;
        fs::Fiber fiber{ infra::MakeRange(stack) };
    };
}

TEST_F(FiberTest, runs_a_body_to_its_end_and_releases_the_claim)
{
    EXPECT_CALL(step, callback(1));
    Run([this]
        {
            step.callback(1);
        });

    EXPECT_FALSE(resource.IsClaimed());
}

TEST_F(FiberTest, a_suspended_body_keeps_the_claim_and_continues_where_it_stopped)
{
    EXPECT_CALL(step, callback(1));
    Run([this]
        {
            int local = 2;
            step.callback(1);
            fiber.Suspend();
            step.callback(local);
        });

    EXPECT_TRUE(resource.IsClaimed());

    EXPECT_CALL(step, callback(2));
    fiber.Resume();
    EXPECT_FALSE(resource.IsClaimed());
}

TEST_F(FiberTest, runs_a_second_body_after_the_first)
{
    EXPECT_CALL(step, callback(1));
    Run([this]
        {
            step.callback(1);
        });

    EXPECT_CALL(step, callback(2));
    Run([this]
        {
            fiber.Suspend();
            step.callback(2);
        });
    fiber.Resume();

    EXPECT_FALSE(resource.IsClaimed());
}

#if !defined(_WIN32)
TEST_F(FiberTest, peak_counts_the_bytes_of_the_stack_that_a_body_used)
{
    EXPECT_EQ(0, fiber.Peak());

    EXPECT_CALL(step, callback(1));
    Run([this]
        {
            step.callback(1);
        });

    EXPECT_GT(fiber.Peak(), 0);
    EXPECT_LT(fiber.Peak(), stack.size());
}
#endif
