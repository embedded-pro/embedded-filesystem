#include "fs/fat/FatChainWalk.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"

namespace
{
    class FatChainWalkTest
        : public FatVolumeFixture
    {
    public:
        FatChainWalkTest()
            : FatVolumeFixture(16384, ClusterSize(1))
        {}

        void ExpectWalk(const fs::FatExtent& from, uint32_t target, uint32_t limit, fs::Result result, const fs::FatExtent& run)
        {
            walk.Walk(from, target, limit, [this](fs::Result result, const fs::FatExtent& run)
                {
                    done.callback(result, run);
                });
            EXPECT_CALL(done, callback(result, run));
            ExecuteAllActions();
        }

        fs::FatChainWalk walk{ cache, geometry };
        testing::StrictMock<infra::MockCallback<void(fs::Result, const fs::FatExtent&)>> done;
    };
}

TEST_F(FatChainWalkTest, contiguous_chain_is_one_run_up_to_the_limit)
{
    auto first = builder->AllocateChain(10);

    ExpectWalk(fs::FatExtent{ 0, first, 1 }, 2, 5, fs::Result::success, fs::FatExtent{ 0, first, 6 });
}

TEST_F(FatChainWalkTest, run_ends_at_the_end_of_the_chain)
{
    auto first = builder->AllocateChain(3);

    ExpectWalk(fs::FatExtent{ 0, first, 1 }, 1, 8, fs::Result::success, fs::FatExtent{ 0, first, 3 });
}

TEST_F(FatChainWalkTest, fragmented_chain_gives_the_run_of_the_target)
{
    auto first = builder->AllocateChain(4, true);

    ExpectWalk(fs::FatExtent{ 0, first, 1 }, 2, 2, fs::Result::success, fs::FatExtent{ 2, first + 4, 1 });
}

TEST_F(FatChainWalkTest, walk_continues_from_a_later_run)
{
    builder->AllocateChain(1);
    auto first = builder->AllocateChain(6, true);

    ExpectWalk(fs::FatExtent{ 3, first + 6, 1 }, 5, 5, fs::Result::success, fs::FatExtent{ 5, first + 10, 1 });
}

TEST_F(FatChainWalkTest, run_stops_at_a_gap_after_the_target)
{
    auto first = builder->AllocateChain(2);
    builder->AllocateChain(1);
    builder->SetFatEntry(first + 1, first + 3);
    builder->SetFatEntry(first + 3, fs::EndOfChain(geometry.type));

    ExpectWalk(fs::FatExtent{ 0, first, 1 }, 1, 2, fs::Result::success, fs::FatExtent{ 0, first, 2 });
}

TEST_F(FatChainWalkTest, chain_shorter_than_the_target_is_corrupted)
{
    auto first = builder->AllocateChain(2, true);

    ExpectWalk(fs::FatExtent{ 0, first, 1 }, 3, 3, fs::Result::corrupted, fs::FatExtent{ 1, first + 2, 1 });
}

TEST_F(FatChainWalkTest, walk_through_a_free_cluster_is_corrupted)
{
    auto first = builder->AllocateChain(1);
    builder->SetFatEntry(first, first + 5);

    walk.Walk(fs::FatExtent{ 0, first, 1 }, 2, 2, [this](fs::Result result, const fs::FatExtent&)
        {
            done.callback(result, fs::FatExtent{});
        });
    EXPECT_CALL(done, callback(fs::Result::corrupted, fs::FatExtent{}));
    ExecuteAllActions();
}

TEST_F(FatChainWalkTest, chain_that_loops_is_corrupted)
{
    auto first = builder->AllocateChain(2);
    builder->SetFatEntry(first + 1, first);

    walk.Walk(fs::FatExtent{ 0, first, 1 }, geometry.clusterCount, geometry.clusterCount, [this](fs::Result result, const fs::FatExtent&)
        {
            done.callback(result, fs::FatExtent{});
        });
    EXPECT_CALL(done, callback(fs::Result::corrupted, fs::FatExtent{}));
    ExecuteAllActions();
}

TEST_F(FatChainWalkTest, device_error_is_reported)
{
    auto first = builder->AllocateChain(2);
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ExpectWalk(fs::FatExtent{ 0, first, 1 }, 1, 1, fs::Result::deviceError, fs::FatExtent{ 0, first, 1 });
}
