#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/test/NorRecordsFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t firstRecord = headerSize + 36;
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    infra::ConstByteRange Bytes(const char* text)
    {
        return infra::StringAsByteRange(infra::BoundedConstString(text));
    }

    class NorJournalTest
        : public fs::NorRecordsFixture<config, 32, blockSize>
    {
    public:
        NorJournalTest()
        {
            Mount();
        }

        fs::Result PutRoot()
        {
            return Commit({ fs::NorInodePut{ root, infra::MakeRange(rootRecord) } });
        }

        std::optional<fs::NorEntry> FirstEntryAt(uint32_t block, uint32_t offset)
        {
            image.Read(block * blockSize + offset, infra::MakeRange(bytes));
            auto length = fs::RecordLength(infra::MakeRange(bytes));
            if (!length.has_value())
                return std::nullopt;

            auto opened = fs::OpenRecord(infra::Head(infra::MakeRange(bytes), *length));
            if (!opened.has_value())
                return std::nullopt;

            return fs::NorEntryReader(*opened, recordBytes).Next();
        }

        void FillBlocksOfTheEpoch()
        {
            while (State().epoch.size() != config.journalBlocks || State().journal.end + putRootBytes <= blockSize)
                ASSERT_EQ(fs::Result::success, PutRoot());
        }

        void FillTheCheckpointBlock()
        {
            FillBlocksOfTheEpoch();
            ASSERT_EQ(fs::Result::success, PutRoot());
            while (State().journal.block == State().checkpointBlock)
                ASSERT_EQ(fs::Result::success, PutRoot());
        }

        static constexpr uint32_t putRootBytes = fs::CrcRecord::overhead + fs::norInodePutFixedBytes + recordBytes;

        void UseEveryFreeBlock()
        {
            for (uint32_t block = 2; block != 32; ++block)
                if (State().blocks.State(block) != fs::NorBlockState::inUse)
                    State().blocks.Set(block, fs::NorBlockState::inUse);
        }

        Record rootRecord{ Serialized(fs::NorInodeRecord{ 0, fs::NorInodeType::directory }) };
        std::array<uint8_t, 512> bytes{};
    };
}

TEST_F(NorJournalTest, the_first_record_after_a_mount_starts_a_journal_block_linked_to_the_end_of_the_records)
{
    EXPECT_EQ(fs::Result::success, PutRoot());

    EXPECT_EQ(4, State().journal.block);
    EXPECT_EQ(2, State().journal.blocks);
    EXPECT_THAT(State().epoch, testing::ElementsAre(3, 4));
    EXPECT_FALSE(State().startNewJournalBlock);
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(4));
    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::journal, 3, {} }), HeaderOf(4).allocation);
    EXPECT_EQ((fs::NorEntry{ fs::NorLink{ 3, firstRecord } }), FirstEntryAt(4, headerSize));
}

TEST_F(NorJournalTest, later_records_follow_in_the_same_block_without_a_link)
{
    PutRoot();
    auto end = State().journal.end;

    EXPECT_EQ(fs::Result::success, PutRoot());

    EXPECT_EQ(4, State().journal.block);
    EXPECT_EQ(end + putRootBytes, State().journal.end);
    EXPECT_EQ((fs::NorEntry{ fs::NorInodePut{ root, infra::MakeRange(rootRecord) } }), FirstEntryAt(4, end));
}

TEST_F(NorJournalTest, the_record_is_applied_when_it_is_written)
{
    auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::directory, 0, 0, 0, {}, Bytes("a") });

    EXPECT_EQ(fs::Result::success, Commit({ fs::NorInodePut{ 2, infra::MakeRange(record) }, fs::NorDirectoryEntryPut{ root, fs::NorDirectoryKey("a"), 2 } }));

    EXPECT_EQ(2, EntryInVolume(root, fs::NorDirectoryKey("a")));
    EXPECT_EQ(3, State().nextInode);
}

TEST_F(NorJournalTest, completes_with_the_result_of_applying_the_record)
{
    EXPECT_EQ(fs::Result::unsupported, Commit({ fs::NorInodeRemove{ 2 } }));
}

TEST_F(NorJournalTest, a_record_that_does_not_fit_in_the_checkpoint_block_starts_a_journal_block)
{
    FillBlocksOfTheEpoch();
    ASSERT_EQ(fs::Result::success, PutRoot());
    auto checkpointBlock = State().checkpointBlock;
    ASSERT_EQ(checkpointBlock, State().journal.block);

    while (State().journal.block == checkpointBlock)
    {
        auto end = State().journal.end;
        ASSERT_EQ(fs::Result::success, PutRoot());
        if (State().journal.block != checkpointBlock)
            EXPECT_EQ((fs::NorEntry{ fs::NorLink{ checkpointBlock, end } }), FirstEntryAt(State().journal.block, headerSize));
    }

    EXPECT_EQ(2, State().journal.blocks);
    EXPECT_EQ(checkpointBlock, State().checkpointBlock);
}

TEST_F(NorJournalTest, a_record_beyond_the_blocks_of_an_epoch_makes_a_checkpoint_first)
{
    FillBlocksOfTheEpoch();

    EXPECT_EQ(fs::Result::success, PutRoot());

    EXPECT_NE(3, State().checkpointBlock);
    EXPECT_EQ(State().checkpointBlock, State().journal.block);
    EXPECT_EQ(1, State().journal.blocks);
    EXPECT_THAT(State().epoch, testing::ElementsAre(State().checkpointBlock));
    EXPECT_EQ((fs::NorEntry{ fs::NorInodePut{ root, infra::MakeRange(rootRecord) } }), FirstEntryAt(State().checkpointBlock, firstRecord));
}

TEST_F(NorJournalTest, a_journal_block_without_a_free_block_has_no_space)
{
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, PutRoot());
    EXPECT_EQ(3, State().journal.block);
}

TEST_F(NorJournalTest, a_checkpoint_for_a_record_without_a_free_block_has_no_space)
{
    FillBlocksOfTheEpoch();
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, PutRoot());
}

TEST_F(NorJournalTest, the_journal_is_more_than_half_full_when_more_than_half_the_room_of_its_epoch_is_taken)
{
    EXPECT_FALSE(Volume().Journal().IsMoreThanHalfFull());

    PutRoot();

    EXPECT_TRUE(Volume().Journal().IsMoreThanHalfFull());
}

TEST_F(NorJournalTest, a_mount_after_a_power_loss_replays_the_records_of_every_block_of_the_epoch)
{
    for (auto path : { "/a", "/b", "/c" })
        ASSERT_EQ(fs::Result::success, MakeDirectory(path));
    FillTheCheckpointBlock();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d/e"));
    auto journal = State().journal;
    ASSERT_EQ(2, journal.blocks);

    fs::ExposedNorFileSystem<config> afterPowerLoss{ flash };
    std::optional<fs::Result> mounted;
    afterPowerLoss.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    ExecuteAllActions();
    EXPECT_EQ(fs::Result::success, mounted);

    const auto& replayed = afterPowerLoss.Volume().State();
    EXPECT_EQ(journal, replayed.journal);
    EXPECT_EQ(7, replayed.nextInode);
    for (auto path : { "/a", "/b", "/c", "/d", "/d/e" })
    {
        std::optional<fs::Result> found;
        afterPowerLoss.Stat(path, [&found](fs::Result result, const fs::EntryInfo&)
            {
                found = result;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, found) << path;
    }
}

TEST_F(NorJournalTest, a_record_that_cannot_be_applied_makes_the_mount_corrupted)
{
    Unmount();
    Builder().AppendRecord({ fs::NorDirectoryEntryPut{ 9, 0x100, 2 } });

    Mount(fs::Result::corrupted);
}

TEST_F(NorJournalTest, a_record_with_an_entry_of_a_later_step_makes_the_mount_unsupported)
{
    Unmount();
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });

    Mount(fs::Result::unsupported);
}

TEST_F(NorJournalTest, unmount_writes_a_checkpoint_when_the_journal_is_more_than_half_full)
{
    while (!Volume().Journal().IsMoreThanHalfFull())
        PutRoot();

    Unmount();

    EXPECT_NE(3, State().checkpointBlock);
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(3));
}

TEST_F(NorJournalTest, unmount_writes_no_checkpoint_when_the_journal_is_at_most_half_full)
{
    Unmount();

    EXPECT_EQ(3, State().checkpointBlock);
}

TEST_F(NorJournalTest, unmount_completes_with_the_result_of_its_checkpoint)
{
    while (!Volume().Journal().IsMoreThanHalfFull())
        PutRoot();
    UseEveryFreeBlock();

    Unmount(fs::Result::noSpace);
}
