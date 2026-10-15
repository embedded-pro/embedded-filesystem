#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/nor/NorVolume.hpp"
#include "fs/nor/test/NorRecordsFixture.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    constexpr fs::NorConfig twoSectors{ .maxBlocks = 22, .sectorsPerBlock = 2, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    class NorBackgroundTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorBackgroundTest()
        {
            auto& state = volume.State();
            state.geometry = fs::MakeNorGeometry(twoSectors, flash);
            state.blocks.Reset(22);
            state.eraseCounts.Reset(22);
            for (uint32_t block = 0; block != 22; ++block)
            {
                state.eraseCounts.Record(block, 5);
                state.blocks.Set(block, fs::NorBlockState::inUse);
            }
            state.eraseCounts.Record(9, 4);
            state.blocks.Set(6, fs::NorBlockState::freeNotErased);
            state.blocks.Set(9, fs::NorBlockState::freeNotErased);
            volume.MarkMounted();
        }

        ~NorBackgroundTest() override
        {
            volume.MarkUnmounted();
        }

        void FinishForegroundCall()
        {
            foreground.Claim([] {});
            ExecuteAllActions();
            foreground.Release();
        }

        void CompleteRequests()
        {
            while (flash.done)
                flash.done();
        }

        std::vector<uint8_t> PartA(uint32_t count) const
        {
            std::array<uint8_t, 16> partA{};
            fs::SerializeNorHeaderA(count, infra::MakeRange(partA));
            return { partA.begin(), partA.end() };
        }

        testing::StrictMock<hal::FlashMock> flash{ 44, 2048 };
        fs::DeferredCompletions::WithMaxSize<2> deferred;
        std::array<uint8_t, fs::NorBlockStateBytes(22)> states{};
        std::array<uint8_t, 22> counts{};
        std::array<uint8_t, fs::NorNodeBlockBytes(22)> nodeBlocks{};
        std::array<uint8_t, 256> journal{};
        std::array<uint8_t, 15 * 512> nodes{};
        std::array<fs::NorNodeCache::Slot, 15> slots{};
        fs::NorVolume volume{ fs::NorVolumeParts{ flash, deferred, infra::MakeRange(states), infra::MakeRange(counts), infra::MakeRange(nodeBlocks), infra::MakeRange(journal), infra::MakeRange(nodes), infra::MakeRange(slots) }, twoSectors, 10ms };
        fs::OperationQueue::Claimer foreground{ volume.Queue() };
    };
}

TEST_F(NorBackgroundTest, does_nothing_before_the_idle_delay)
{
    FinishForegroundCall();

    ForwardTime(9ms);
}

TEST_F(NorBackgroundTest, after_the_idle_delay_erases_the_free_blocks_that_are_not_erased_least_worn_first_one_sector_at_a_time)
{
    FinishForegroundCall();

    testing::InSequence sequence;
    EXPECT_CALL(flash, eraseSectorsMock(18, 19));
    EXPECT_CALL(flash, eraseSectorsMock(19, 20));
    EXPECT_CALL(flash, writeBufferMock(PartA(5), 9 * 4096));
    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    EXPECT_CALL(flash, eraseSectorsMock(13, 14));
    EXPECT_CALL(flash, writeBufferMock(PartA(6), 6 * 4096));
    ForwardTime(10ms);
    CompleteRequests();

    EXPECT_EQ(fs::NorBlockState::freeErased, volume.State().blocks.State(6));
    EXPECT_EQ(fs::NorBlockState::freeErased, volume.State().blocks.State(9));
}

TEST_F(NorBackgroundTest, yields_to_a_waiting_call_after_one_sector_and_continues_after_the_next_idle_delay)
{
    FinishForegroundCall();
    EXPECT_CALL(flash, eraseSectorsMock(18, 19));
    ForwardTime(10ms);

    testing::StrictMock<infra::MockCallback<void()>> granted;
    foreground.Claim([&granted]
        {
            granted.callback();
        });
    EXPECT_CALL(granted, callback());
    CompleteRequests();
    ExecuteAllActions();
    foreground.Release();

    testing::InSequence sequence;
    EXPECT_CALL(flash, eraseSectorsMock(19, 20));
    EXPECT_CALL(flash, writeBufferMock(PartA(5), 9 * 4096));
    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    EXPECT_CALL(flash, eraseSectorsMock(13, 14));
    EXPECT_CALL(flash, writeBufferMock(PartA(6), 6 * 4096));
    ForwardTime(10ms);
    CompleteRequests();
}

TEST_F(NorBackgroundTest, the_sector_that_a_waiting_call_waited_for_counts_as_the_refill_of_that_call)
{
    for (uint32_t block = 10; block != 20; ++block)
        volume.State().blocks.Set(block, fs::NorBlockState::freeNotErased);
    FinishForegroundCall();
    EXPECT_CALL(flash, eraseSectorsMock(18, 19));
    ForwardTime(10ms);

    foreground.Claim([this]
        {
            volume.Eraser().Refill([] {});
        });
    CompleteRequests();
    ExecuteAllActions();
    foreground.Release();
    testing::Mock::VerifyAndClearExpectations(&flash);
}

TEST_F(NorBackgroundTest, a_claim_granted_while_a_call_waits_yields_before_any_work)
{
    testing::StrictMock<infra::MockCallback<void()>> granted;
    infra::TimerSingleShot arrival{ 10ms, [this, &granted]
        {
            foreground.Claim([&granted]
                {
                    granted.callback();
                });
        } };
    FinishForegroundCall();

    EXPECT_CALL(granted, callback());
    ForwardTime(10ms);
    foreground.Release();

    testing::InSequence sequence;
    EXPECT_CALL(flash, eraseSectorsMock(18, 19));
    EXPECT_CALL(flash, eraseSectorsMock(19, 20));
    EXPECT_CALL(flash, writeBufferMock(PartA(5), 9 * 4096));
    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    EXPECT_CALL(flash, eraseSectorsMock(13, 14));
    EXPECT_CALL(flash, writeBufferMock(PartA(6), 6 * 4096));
    ForwardTime(10ms);
    CompleteRequests();
}

TEST_F(NorBackgroundTest, an_unmount_forgets_the_block_in_erasure)
{
    FinishForegroundCall();
    EXPECT_CALL(flash, eraseSectorsMock(18, 19));
    volume.Eraser().Step([] {});
    CompleteRequests();
    volume.State().eraseCounts.Record(6, 1);

    volume.MarkUnmounted();
    volume.MarkMounted();

    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    volume.Eraser().Step([] {});
    CompleteRequests();
}

TEST_F(NorBackgroundTest, stops_when_the_volume_is_unmounted)
{
    FinishForegroundCall();
    volume.MarkUnmounted();

    ForwardTime(1s);
}

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    class NorBackgroundCheckpointTest
        : public fs::NorRecordsFixture<config, 32, 4096>
    {
    public:
        NorBackgroundCheckpointTest()
        {
            AddDirectories(root, 40);
            Mount();
        }

        void FillJournalOverHalf()
        {
            auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::directory, 0, 0, 0, {}, infra::StringAsByteRange(infra::BoundedConstString("m")) });
            while (!Volume().Journal().IsMoreThanHalfFull())
                ASSERT_EQ(fs::Result::success, Commit({ fs::NorInodePut{ 2, infra::MakeRange(record) } }));
        }

        void KeepErased(uint32_t erased)
        {
            for (uint32_t block = 2; block != 32; ++block)
                if (State().blocks.State(block) == fs::NorBlockState::freeNotErased)
                    State().blocks.Set(block, fs::NorBlockState::inUse);
                else if (State().blocks.State(block) != fs::NorBlockState::freeErased)
                    continue;
                else if (erased == 0)
                    State().blocks.Set(block, fs::NorBlockState::inUse);
                else
                    --erased;
        }

        void Idle()
        {
            fs::OperationQueue::Claimer claimer{ Volume().Queue() };
            claimer.Claim([] {});
            ExecuteAllActions();
            claimer.Release();
            ForwardTime(10ms);
        }
    };
}

TEST_F(NorBackgroundCheckpointTest, writes_a_checkpoint_when_the_journal_is_more_than_half_full)
{
    FillJournalOverHalf();
    auto checkpointBlock = State().checkpointBlock;

    Idle();

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_FALSE(Volume().Journal().IsMoreThanHalfFull());
}

TEST_F(NorBackgroundCheckpointTest, writes_a_checkpoint_when_more_than_half_the_cache_is_modified)
{
    ModifyLeavesOf({ 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40 });
    ASSERT_LT(Volume().Cache().Capacity(), 2 * Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    Idle();

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(0, Volume().Cache().ModifiedNodes());
}

TEST_F(NorBackgroundCheckpointTest, writes_no_checkpoint_until_the_pool_holds_every_block_it_may_need)
{
    FillJournalOverHalf();
    KeepErased(Volume().Geometry().NodeBlocksPerCheckpoint());
    auto checkpointBlock = State().checkpointBlock;

    Idle();

    EXPECT_EQ(checkpointBlock, State().checkpointBlock);
}

TEST_F(NorBackgroundCheckpointTest, erases_a_free_block_so_that_a_checkpoint_that_waits_for_the_pool_follows)
{
    FillJournalOverHalf();
    KeepErased(Volume().Geometry().NodeBlocksPerCheckpoint());
    State().blocks.Set(30, fs::NorBlockState::freeNotErased);
    auto checkpointBlock = State().checkpointBlock;

    Idle();

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
}

namespace
{
    class NorBackgroundCollectionTest
        : public fs::NorRecordsFixture<config, 32, 4096>
    {
    public:
        void Idle()
        {
            fs::OperationQueue::Claimer claimer{ Volume().Queue() };
            claimer.Claim([] {});
            ExecuteAllActions();
            claimer.Release();
            ForwardTime(10ms);
        }

        void KeepErased(uint32_t erased)
        {
            for (uint32_t block = 2; block != 32; ++block)
                if (State().blocks.State(block) == fs::NorBlockState::freeErased && erased != 0)
                    --erased;
                else if (State().blocks.State(block) != fs::NorBlockState::inUse)
                    State().blocks.Set(block, fs::NorBlockState::inUse);
        }
    };
}

TEST_F(NorBackgroundCollectionTest, writes_a_checkpoint_when_a_collection_is_due)
{
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Mount();
    ASSERT_TRUE(Volume().Collector().Due());
    auto checkpointBlock = State().checkpointBlock;

    Idle();

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_FALSE(State().nodeBlocks.Contains(2));
    EXPECT_FALSE(Volume().Collector().Due());
}

TEST_F(NorBackgroundCollectionTest, a_checkpoint_with_a_collection_waits_for_the_pool_to_hold_twice_the_node_blocks_of_a_checkpoint)
{
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Mount();
    ASSERT_TRUE(Volume().Collector().Due());
    KeepErased(2 * Volume().Geometry().NodeBlocksPerCheckpoint());
    auto checkpointBlock = State().checkpointBlock;

    Idle();

    EXPECT_EQ(checkpointBlock, State().checkpointBlock);

    State().blocks.Set(30, fs::NorBlockState::freeErased);
    Idle();

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
}

TEST_F(NorBackgroundCollectionTest, collects_at_most_one_round_of_the_node_blocks_between_two_calls)
{
    AddDirectories(root, 40);
    Mount();
    State().liveNodes = 0;
    auto passed = Volume().Collector().LeftBlocks();
    auto round = Volume().Collector().CollectableBlocks();

    Idle();

    EXPECT_TRUE(Volume().Collector().Due());
    EXPECT_LE(passed + round, Volume().Collector().LeftBlocks());
    EXPECT_GT(passed + round + 2 * Volume().Geometry().NodeBlocksPerCheckpoint(), Volume().Collector().LeftBlocks());

    passed = Volume().Collector().LeftBlocks();
    ForwardTime(1s);

    EXPECT_EQ(passed, Volume().Collector().LeftBlocks());

    Idle();

    EXPECT_LT(passed, Volume().Collector().LeftBlocks());
}

TEST_F(NorBackgroundCollectionTest, a_block_whose_nodes_cannot_be_checked_counts_in_the_round)
{
    AddDirectories(root, 40);
    std::array<uint8_t, 512> rootNode{};
    Builder().Read(Builder().InodeTable().address, infra::MakeRange(rootNode));
    Builder().Corrupt(fs::NorBTree::ChildFor(infra::MakeRange(rootNode), 4) + 12);
    Mount();
    State().liveNodes = 0;
    auto left = Volume().Collector().LeftBlocks();

    Idle();

    EXPECT_TRUE(Volume().Collector().Due());
    EXPECT_LT(left, Volume().Collector().LeftBlocks());

    left = Volume().Collector().LeftBlocks();
    ForwardTime(1s);

    EXPECT_EQ(left, Volume().Collector().LeftBlocks());
}
