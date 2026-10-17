#include "fs/nor/test/NorFilesFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

using namespace std::chrono_literals;

namespace
{
    constexpr fs::NorConfig levelingConfig{ .maxBlocks = 48, .cachedNodes = 15, .staticWearLevelingThreshold = 4, .staticWearLevelingInterval = 2 };
    constexpr fs::NorFeatures withoutBackground{ .background = false };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    class NorForegroundLevelingTest
        : public fs::NorFilesFixture<levelingConfig, 48, 4096, withoutBackground>
    {
    public:
        NorForegroundLevelingTest()
        {
            file = Builder().AddFile(root, "f", infra::MakeRange(contents));
            Builder().Commit();
            Mount();
        }

        void SpreadTheWear()
        {
            for (uint32_t block = 2; block != 48; ++block)
                State().eraseCounts.Record(block, 10);
            State().eraseCounts.Record(BlockInVolume(file, 0), 1);
            State().eraseCounts.Record(MostWornCandidate(), 30);
        }

        uint32_t MostWornCandidate()
        {
            for (uint32_t block = 47; block != 2; --block)
                if (State().blocks.State(block) == fs::NorBlockState::freeErased)
                    return block;

            return 0;
        }

        fs::Result StatOfFile()
        {
            return Stat("/f").first;
        }

        std::array<uint8_t, 100> contents{ fs::Pattern<100>() };
        std::array<uint8_t, 4200> written{ fs::Pattern<4200>(3) };
        uint32_t file{ 0 };
    };
}

TEST_F(NorForegroundLevelingTest, the_first_call_after_a_mount_moves_a_block_when_the_spread_exceeds_the_threshold)
{
    SpreadTheWear();
    auto target = MostWornCandidate();

    EXPECT_EQ(fs::Result::success, StatOfFile());

    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    EXPECT_EQ(target, BlockInVolume(file, 0));
}

TEST_F(NorForegroundLevelingTest, the_volume_is_idle_as_soon_as_the_move_after_a_call_is_done)
{
    SpreadTheWear();

    EXPECT_EQ(fs::Result::success, StatOfFile());

    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    EXPECT_TRUE(this->fileSystem.Idle());
}

TEST_F(NorForegroundLevelingTest, makes_no_move_while_the_spread_is_within_the_threshold)
{
    EXPECT_EQ(fs::Result::success, StatOfFile());

    EXPECT_EQ(0, Volume().Counters().movedBlocks);
}

TEST_F(NorForegroundLevelingTest, looks_for_a_move_again_after_a_call_that_allocates_a_block)
{
    ASSERT_EQ(fs::Result::success, StatOfFile());
    SpreadTheWear();
    ASSERT_EQ(fs::Result::success, StatOfFile());
    ASSERT_EQ(0, Volume().Counters().movedBlocks);

    auto created = Open("/g", fs::OpenMode::write);

    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    Close(*created);
}

TEST_F(NorForegroundLevelingTest, after_a_move_makes_no_move_before_the_interval_of_erases)
{
    SpreadTheWear();
    ASSERT_EQ(fs::Result::success, StatOfFile());
    ASSERT_EQ(1, Volume().Counters().movedBlocks);
    State().eraseCounts.Record(MostWornCandidate(), 40);
    Volume().Counters().erasedSectors = 0;
    auto created = Open("/g", fs::OpenMode::write);

    ASSERT_EQ(fs::Result::success, Write(*created, infra::MakeRange(written)));
    EXPECT_EQ(1, Volume().Counters().movedBlocks);

    Volume().Counters().erasedSectors += 2;
    ASSERT_EQ(fs::Result::success, Write(*created, infra::MakeRange(written)));
    EXPECT_EQ(2, Volume().Counters().movedBlocks);
    Close(*created);
}

TEST_F(NorForegroundLevelingTest, a_call_that_waits_for_the_queue_waits_for_the_move)
{
    SpreadTheWear();
    std::optional<uint32_t> movedBeforeSecondCall;

    fileSystem.Stat("/f", [](fs::Result, const fs::EntryInfo&) {});
    fileSystem.Stat("/f", [this, &movedBeforeSecondCall](fs::Result, const fs::EntryInfo&)
        {
            movedBeforeSecondCall = Volume().Counters().movedBlocks;
        });
    ExecuteAllActions();

    EXPECT_EQ(1, movedBeforeSecondCall);
}

TEST_F(NorForegroundLevelingTest, makes_no_move_while_the_free_blocks_cannot_hold_twice_the_node_blocks_of_a_checkpoint)
{
    SpreadTheWear();
    LeaveFreeBlocks(2 * Volume().Geometry().NodeBlocksPerCheckpoint());

    EXPECT_EQ(fs::Result::success, StatOfFile());

    EXPECT_EQ(0, Volume().Counters().movedBlocks);
    EXPECT_TRUE(Volume().Leveler().Due());
}

TEST_F(NorForegroundLevelingTest, erases_nothing_while_the_volume_is_idle)
{
    for (uint32_t block = 2; block != 48; ++block)
        if (State().blocks.State(block) == fs::NorBlockState::freeErased)
            State().blocks.Set(block, fs::NorBlockState::freeNotErased);
    ASSERT_EQ(fs::Result::success, StatOfFile());

    ForwardTime(1s);

    EXPECT_EQ(0, Volume().Counters().erasedSectors);
}

TEST_F(NorForegroundLevelingTest, an_unmount_withdraws_the_move_that_its_release_claims)
{
    SpreadTheWear();

    Unmount();
    ExecuteAllActions();

    EXPECT_EQ(0, Volume().Counters().movedBlocks);
    Mount();
    EXPECT_EQ(fs::Result::success, StatOfFile());
}
