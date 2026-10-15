#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/nor/NorVolume.hpp"
#include "fs/nor/test/NorFilesFixture.hpp"
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

namespace
{
    constexpr fs::NorConfig levelingConfig{ .maxBlocks = 48, .cachedNodes = 15, .staticWearLevelingThreshold = 4, .staticWearLevelingInterval = 2 };

    class NorBackgroundMoveTest
        : public fs::NorFilesFixture<levelingConfig, 48, 4096>
    {
    public:
        NorBackgroundMoveTest()
        {
            file = Builder().AddFile(root, "f", infra::MakeRange(contents));
            Builder().Commit();
            Mount();
            Idle();
            for (uint32_t block = 2; block != 48; ++block)
                State().eraseCounts.Record(block, 10);
            State().eraseCounts.Record(BlockInVolume(file, 0), 1);
            State().eraseCounts.Record(MostWornCandidate(), 30);
        }

        void Idle()
        {
            fs::OperationQueue::Claimer claimer{ Volume().Queue() };
            claimer.Claim([] {});
            ExecuteAllActions();
            claimer.Release();
            ForwardTime(10ms);
        }

        uint32_t MostWornCandidate()
        {
            for (uint32_t block = 47; block != 2; --block)
                if (State().blocks.State(block) == fs::NorBlockState::freeErased)
                    return block;

            return 0;
        }

        infra::SharedPtr<fs::File> OpenNewFile()
        {
            return Open("/g", fs::OpenMode::write);
        }

        void KeepErased(uint32_t erased)
        {
            for (uint32_t block = 47; block != 1; --block)
                if (State().blocks.State(block) != fs::NorBlockState::freeErased)
                    continue;
                else if (erased != 0)
                    --erased;
                else
                    State().blocks.Set(block, fs::NorBlockState::freeNotErased);
        }

        void ExecuteUntilAllocated()
        {
            auto allocated = Volume().Counters().allocatedBlocks;
            while (Volume().Counters().allocatedBlocks == allocated && !IsIdle())
                ExecuteFirstAction();
            ASSERT_NE(allocated, Volume().Counters().allocatedBlocks);
        }

        std::array<uint8_t, 100> contents{ fs::Pattern<100>() };
        std::array<uint8_t, 4200> written{ fs::Pattern<4200>(3) };
        uint32_t file{ 0 };
    };
}

TEST_F(NorBackgroundMoveTest, after_a_mount_moves_a_block_as_soon_as_the_spread_exceeds_the_threshold)
{
    auto target = MostWornCandidate();

    Idle();

    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    EXPECT_EQ(target, BlockInVolume(file, 0));
}

TEST_F(NorBackgroundMoveTest, after_a_move_makes_no_move_before_the_interval_of_erases)
{
    Idle();
    ASSERT_EQ(1, Volume().Counters().movedBlocks);
    for (uint32_t block = 2; block != 48; ++block)
        if (State().nodeBlocks.Contains(block) && State().blocks.State(block) == fs::NorBlockState::inUse && block != State().nodeBlock)
            State().eraseCounts.Record(block, 1);
    State().eraseCounts.Record(MostWornCandidate(), 40);

    Idle();
    EXPECT_EQ(1, Volume().Counters().movedBlocks);

    Volume().Counters().erasedSectors += 2;
    Idle();
    EXPECT_EQ(2, Volume().Counters().movedBlocks);
}

TEST_F(NorBackgroundMoveTest, the_fallback_moves_a_block_right_after_the_call_whose_allocation_reached_twice_the_interval)
{
    auto target = MostWornCandidate();
    auto created = OpenNewFile();
    Volume().Counters().erasedSectors = 4;

    ASSERT_EQ(fs::Result::success, Write(*created, infra::MakeRange(written)));

    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    EXPECT_EQ(target, BlockInVolume(file, 0));
    Close(*created);
}

TEST_F(NorBackgroundMoveTest, the_fallback_moves_a_block_before_a_call_that_waits)
{
    auto created = OpenNewFile();
    Volume().Counters().erasedSectors = 4;
    std::optional<uint32_t> movedBeforeStat;

    created->Write(infra::MakeRange(written), [this, &movedBeforeStat](fs::Result)
        {
            fileSystem.Stat("/f", [this, &movedBeforeStat](fs::Result, const fs::EntryInfo&)
                {
                    movedBeforeStat = Volume().Counters().movedBlocks;
                });
        });
    ExecuteAllActions();

    EXPECT_EQ(1, movedBeforeStat);
    Close(*created);
}

TEST_F(NorBackgroundMoveTest, no_fallback_before_twice_the_interval_of_erases)
{
    auto created = OpenNewFile();
    Volume().Counters().erasedSectors = 3;

    ASSERT_EQ(fs::Result::success, Write(*created, infra::MakeRange(written)));

    EXPECT_EQ(0, Volume().Counters().movedBlocks);
    Close(*created);
}

TEST_F(NorBackgroundMoveTest, the_fallback_moves_a_block_before_a_call_that_waited_for_the_call_that_armed_it)
{
    auto created = OpenNewFile();
    Volume().Counters().erasedSectors = 4;
    std::optional<uint32_t> movedBeforeStat;

    created->Write(infra::MakeRange(written), [](fs::Result) {});
    fileSystem.Stat("/f", [this, &movedBeforeStat](fs::Result, const fs::EntryInfo&)
        {
            movedBeforeStat = Volume().Counters().movedBlocks;
        });
    ExecuteAllActions();

    EXPECT_EQ(1, movedBeforeStat);
    Close(*created);
}

TEST_F(NorBackgroundMoveTest, an_unmount_whose_checkpoint_arms_the_fallback_stops_the_background)
{
    Volume().Counters().erasedSectors = 0;
    auto created = OpenNewFile();
    std::array<uint8_t, 1> byte{ 1 };
    while (!Volume().Journal().IsMoreThanHalfFull())
    {
        ASSERT_EQ(fs::Result::success, Write(*created, infra::MakeRange(byte)));
        ASSERT_EQ(fs::Result::success, Sync(*created));
    }
    Volume().Counters().erasedSectors = 4;

    Unmount();

    EXPECT_EQ(0, Volume().Counters().movedBlocks);
}

TEST_F(NorBackgroundMoveTest, an_allocation_of_the_background_does_not_arm_the_fallback_ahead_of_a_waiting_call)
{
    Volume().Counters().erasedSectors = 0;
    auto created = OpenNewFile();
    std::array<uint8_t, 1> byte{ 1 };
    while (!Volume().Journal().IsMoreThanHalfFull())
    {
        ASSERT_EQ(fs::Result::success, Write(*created, infra::MakeRange(byte)));
        ASSERT_EQ(fs::Result::success, Sync(*created));
    }
    Close(*created);
    Volume().Counters().erasedSectors = 4;
    std::optional<uint32_t> movedBeforeStat;

    systemTimerService.TimeProgressed(10ms);
    ExecuteUntilAllocated();
    fileSystem.Stat("/f", [this, &movedBeforeStat](fs::Result, const fs::EntryInfo&)
        {
            movedBeforeStat = Volume().Counters().movedBlocks;
        });
    ExecuteAllActions();

    EXPECT_EQ(0, movedBeforeStat);
}

TEST_F(NorBackgroundMoveTest, a_fallback_that_is_no_longer_due_when_its_call_releases_the_queue_claims_nothing)
{
    auto created = OpenNewFile();
    KeepErased(46);
    for (uint32_t block = 2; block != 48; ++block)
        if (State().blocks.State(block) == fs::NorBlockState::freeErased && block != MostWornCandidate())
        {
            State().blocks.Set(block, fs::NorBlockState::freeNotErased);
            break;
        }
    Volume().Counters().erasedSectors = 4;

    created->Write(infra::MakeRange(written), [](fs::Result) {});
    ExecuteUntilAllocated();
    Volume().Counters().erasedSectors = 0;
    ExecuteAllActions();

    EXPECT_EQ(0, Volume().Counters().erasedSectors);
    EXPECT_EQ(0, Volume().Counters().movedBlocks);
    Close(*created);
}

TEST_F(NorBackgroundMoveTest, the_fallback_moves_into_the_most_worn_free_block_when_the_pool_is_empty)
{
    auto target = MostWornCandidate();
    auto created = OpenNewFile();
    KeepErased(0);
    Volume().Counters().erasedSectors = 4;

    ASSERT_EQ(fs::Result::success, Write(*created, infra::MakeRange(written)));

    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    EXPECT_EQ(target, BlockInVolume(file, 0));
    Close(*created);
}
