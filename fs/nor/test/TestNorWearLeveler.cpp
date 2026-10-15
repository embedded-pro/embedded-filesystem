#include "fs/nor/test/NorFilesFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>

namespace
{
    using fs::Pattern;

    constexpr fs::NorConfig config{ .maxBlocks = 48, .cachedNodes = 15, .staticWearLevelingThreshold = 4, .staticWearLevelingInterval = 2 };
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t payload = blockSize - headerSize;
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;
    constexpr uint32_t worn = 30;

    class NorWearLevelerTest
        : public fs::NorFilesFixture<config, 48, blockSize>
    {
    public:
        static constexpr std::size_t fileSize = payload + 2000;

        fs::NorWearLeveler& Leveler()
        {
            return Volume().Leveler();
        }

        void Wear(uint32_t count)
        {
            for (uint32_t block = 2; block != 48; ++block)
                State().eraseCounts.Record(block, count);
        }

        void WearAllButDataBlocks()
        {
            Wear(10);
            for (uint32_t block = 2; block != 48; ++block)
                if (State().blocks.State(block) == fs::NorBlockState::inUse && !State().nodeBlocks.Contains(block))
                    State().eraseCounts.Record(block, 1);
        }

        void SetErases(uint32_t erases)
        {
            Volume().Counters().erasedSectors = erases;
        }

        void ExpectNoMove(uint32_t target)
        {
            EXPECT_EQ(fs::NorBlockState::freeErased, State().blocks.State(target));
            EXPECT_EQ(0, Volume().Counters().movedBlocks);
            EXPECT_FALSE(Leveler().Due());
        }

        uint32_t FreeErasedBlock()
        {
            for (uint32_t block = 47; block != 2; --block)
                if (State().blocks.State(block) == fs::NorBlockState::freeErased)
                    return block;

            return 0;
        }

        void Move()
        {
            bool moved = false;
            Leveler().Move([&moved]
                {
                    moved = true;
                });
            EXPECT_FALSE(moved);
            ExecuteAllActions();
            EXPECT_TRUE(moved);
        }

        void RemountWithFreeBlocksErased()
        {
            Unmount();
            for (uint32_t block = 2; block != 48; ++block)
                if (State().blocks.State(block) == fs::NorBlockState::freeNotErased)
                    Builder().EraseBlock(block);
            Mount();
        }

        uint32_t MountWithFile()
        {
            auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
            Builder().Commit();
            Mount();
            return file;
        }

        uint32_t MountWithDirectories()
        {
            auto directory = Builder().AddDirectory(root, "a");
            Builder().AddDirectory(directory, "x");
            Builder().Commit();
            Mount();
            return directory;
        }

        std::array<uint8_t, fileSize> contents{ Pattern<fileSize>() };
    };
}

TEST_F(NorWearLevelerTest, the_first_move_after_a_mount_is_due_as_soon_as_the_spread_exceeds_the_threshold)
{
    auto file = MountWithFile();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 0), 5);

    EXPECT_TRUE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, after_a_move_the_next_one_is_due_after_the_interval_of_erases)
{
    auto file = MountWithFile();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 0), 2);
    State().eraseCounts.Record(BlockInVolume(file, 1), 1);
    State().eraseCounts.Record(FreeErasedBlock(), worn);
    SetErases(5);
    Move();

    SetErases(6);
    EXPECT_FALSE(Leveler().Due());
    SetErases(7);
    EXPECT_TRUE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, no_move_is_due_while_the_spread_is_within_the_threshold)
{
    auto file = MountWithFile();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 0), 6);
    SetErases(2);

    EXPECT_FALSE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, a_young_free_block_makes_no_move_due)
{
    MountWithFile();
    Wear(10);
    State().eraseCounts.Record(FreeErasedBlock(), 1);
    SetErases(2);

    EXPECT_FALSE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, no_move_is_due_without_a_free_block_more_worn_than_the_least_worn_block_in_use)
{
    auto file = MountWithFile();
    Wear(1);
    State().eraseCounts.Record(BlockInVolume(file, 1), 20);
    SetErases(2);

    EXPECT_FALSE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, checkpoint_and_journal_blocks_and_the_node_block_being_filled_are_not_moved)
{
    MountWithFile();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    std::optional<fs::Result> written;
    Volume().Checkpoints().Write([&written](fs::Result result)
        {
            written = result;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, written);
    ASSERT_EQ(fs::Result::success, MakeDirectory("/e"));
    Wear(10);
    State().eraseCounts.Record(State().checkpointBlock, 1);
    State().eraseCounts.Record(State().epoch.back(), 1);
    State().eraseCounts.Record(State().nodeBlock.value_or(0), 1);
    SetErases(2);

    EXPECT_FALSE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, the_fallback_is_due_after_twice_the_interval_of_erases)
{
    auto file = MountWithFile();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 0), 1);

    SetErases(3);
    EXPECT_TRUE(Leveler().Due());
    EXPECT_FALSE(Leveler().FallbackDue());
    SetErases(4);
    EXPECT_TRUE(Leveler().FallbackDue());
}

TEST_F(NorWearLevelerTest, moves_the_least_worn_data_block_to_the_most_worn_erased_block)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 1);
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 0), 2);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);

    Move();

    EXPECT_EQ(target, BlockInVolume(file, 1));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(source));
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(target));
    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    ASSERT_TRUE(HeaderOf(target).allocation.has_value());
    EXPECT_EQ((fs::NorBlockOwner{ file, 1 }), HeaderOf(target).allocation->owner);
    EXPECT_FALSE(Leveler().Due());
    RemountWithFreeBlocksErased();
    ExpectContents("/f", contents);
}

TEST_F(NorWearLevelerTest, a_moved_data_block_holds_only_the_committed_bytes)
{
    auto file = MountWithFile();
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 1), 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);
    device.recording = true;

    Move();

    std::size_t programmed = 0;
    for (const auto& request : device.requests)
        if (!request.erase && request.address >= target * blockSize + headerSize && request.address < (target + 1) * blockSize)
            programmed += request.size;
    EXPECT_EQ(fileSize - payload, programmed);
}

TEST_F(NorWearLevelerTest, gives_up_the_block_that_an_open_file_appends_into)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 1);
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(FreeErasedBlock(), worn);
    SetErases(2);
    auto opened = Open("/f", fs::OpenMode::append);

    Move();

    EXPECT_EQ(source, BlockInVolume(file, 1));
    EXPECT_EQ(0, Volume().Counters().movedBlocks);
    EXPECT_FALSE(Leveler().Due());
    Close(*opened);
}

TEST_F(NorWearLevelerTest, moves_a_block_of_an_open_file_other_than_the_one_it_appends_into)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 0);
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);
    auto opened = Open("/f", fs::OpenMode::append);

    Move();

    EXPECT_EQ(target, BlockInVolume(file, 0));
    Close(*opened);
    RemountWithFreeBlocksErased();
    ExpectContents("/f", contents);
}

TEST_F(NorWearLevelerTest, gives_up_a_block_that_an_uncommitted_overwrite_replaces)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 0);
    auto target = FreeErasedBlock();
    auto opened = Open("/f", fs::OpenMode::readWrite);
    std::array<uint8_t, 300> patch{ Pattern<300>(7) };
    ASSERT_EQ(fs::Result::success, Write(*opened, infra::MakeRange(patch)));
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);

    Move();

    ExpectNoMove(target);
    Close(*opened);
    std::ranges::copy(patch, contents.begin());
    ExpectContents("/f", contents);
}

TEST_F(NorWearLevelerTest, the_next_move_passes_over_the_block_given_up_last)
{
    auto file = MountWithFile();
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 0), 2);
    State().eraseCounts.Record(BlockInVolume(file, 1), 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);
    auto opened = Open("/f", fs::OpenMode::append);
    Move();
    ASSERT_EQ(0, Volume().Counters().movedBlocks);

    SetErases(4);
    Move();

    EXPECT_EQ(target, BlockInVolume(file, 0));
    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    Close(*opened);
}

TEST_F(NorWearLevelerTest, moves_a_node_block_into_a_node_block_taken_from_the_most_worn_erased_block)
{
    MountWithDirectories();
    auto source = State().inodeTable.address / blockSize;
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);

    Move();

    EXPECT_FALSE(State().nodeBlocks.Contains(source));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(source));
    EXPECT_TRUE(State().nodeBlocks.Contains(target));
    EXPECT_EQ(target, State().inodeTable.address / blockSize);
    EXPECT_EQ(1, Volume().Counters().movedBlocks);
    RemountWithFreeBlocksErased();
    EXPECT_THAT(List("/a"), testing::ElementsAre("x"));
}

TEST_F(NorWearLevelerTest, a_move_is_due_into_a_worn_free_block_that_is_not_erased)
{
    auto file = MountWithFile();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 1), 1);
    for (uint32_t block = 2; block != 48; ++block)
        if (State().blocks.State(block) == fs::NorBlockState::freeErased)
            State().blocks.Set(block, fs::NorBlockState::freeNotErased);
    SetErases(2);

    EXPECT_TRUE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, no_move_is_due_without_a_free_block)
{
    auto file = MountWithFile();
    Wear(10);
    State().eraseCounts.Record(BlockInVolume(file, 1), 1);
    UseEveryFreeBlock();
    SetErases(2);

    EXPECT_FALSE(Leveler().Due());
}

TEST_F(NorWearLevelerTest, a_data_move_erases_its_destination_when_the_most_worn_free_block_is_not_erased)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 1);
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    State().blocks.Set(target, fs::NorBlockState::freeNotErased);
    SetErases(2);

    Move();

    EXPECT_EQ(target, BlockInVolume(file, 1));
    EXPECT_EQ(worn + 1, HeaderOf(target).eraseCount);
    RemountWithFreeBlocksErased();
    ExpectContents("/f", contents);
}

TEST_F(NorWearLevelerTest, gives_up_a_block_whose_header_names_no_data_block)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 1);
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);
    Builder().Corrupt(source * blockSize + Volume().Geometry().HeaderPartBOffset());

    Move();

    ExpectNoMove(target);
}

TEST_F(NorWearLevelerTest, gives_up_a_data_block_whose_owner_cannot_be_read)
{
    Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Builder().Corrupt(Builder().InodeTable().address + 2);
    Mount();
    auto target = FreeErasedBlock();
    WearAllButDataBlocks();
    State().eraseCounts.Record(target, worn);
    SetErases(2);

    Move();

    ExpectNoMove(target);
}

TEST_F(NorWearLevelerTest, gives_up_a_data_block_whose_map_cannot_be_read)
{
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(file).address + 2);
    Mount();
    auto target = FreeErasedBlock();
    WearAllButDataBlocks();
    State().eraseCounts.Record(target, worn);
    SetErases(2);

    Move();

    ExpectNoMove(target);
}

TEST_F(NorWearLevelerTest, gives_up_when_no_free_block_above_the_reserve_is_left)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 1);
    LeaveFreeBlocks(Volume().Geometry().ReserveBlocks());
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);

    Move();

    EXPECT_EQ(source, BlockInVolume(file, 1));
    ExpectNoMove(target);
}

TEST_F(NorWearLevelerTest, a_move_whose_record_fails_frees_its_target_and_keeps_the_source)
{
    auto file = MountWithFile();
    auto source = BlockInVolume(file, 1);
    auto target = FreeErasedBlock();
    Wear(10);
    State().eraseCounts.Record(source, 1);
    State().eraseCounts.Record(target, worn);
    SetErases(2);
    State().startNewJournalBlock = true;

    bool moved = false;
    Leveler().Move([&moved]
        {
            moved = true;
        });
    EXPECT_FALSE(moved);
    while (State().blocks.State(target) != fs::NorBlockState::pending)
        ExecuteFirstAction();
    for (uint32_t block = 2; block != 48; ++block)
        if (State().blocks.State(block) == fs::NorBlockState::freeErased || State().blocks.State(block) == fs::NorBlockState::freeNotErased)
            State().blocks.Set(block, fs::NorBlockState::inUse);
    ExecuteAllActions();

    EXPECT_TRUE(moved);
    EXPECT_EQ(source, BlockInVolume(file, 1));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(target));
    EXPECT_EQ(0, Volume().Counters().movedBlocks);
}
