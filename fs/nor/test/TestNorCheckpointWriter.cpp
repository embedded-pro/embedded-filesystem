#include "fs/common/DeferredCompletions.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/test/NorVolumeFixture.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t firstRecord = headerSize + 36;
    constexpr uint32_t nodeSlots = 256;
    constexpr uint32_t nodeSize = 512;

    class NorCheckpointWriterTest
        : public fs::NorVolumeFixture<config, 32, blockSize>
    {
    public:
        NorCheckpointWriterTest()
        {
            Mount();
        }

        fs::Result WriteCheckpoint()
        {
            std::optional<fs::Result> written;
            Volume().Checkpoints().Write([&written](fs::Result result)
                {
                    written = result;
                });
            ExecuteAllActions();
            return written.value_or(fs::Result::busy);
        }

        std::array<uint8_t, 36> CheckpointOnFlash(uint32_t block) const
        {
            std::array<uint8_t, 36> bytes{};
            image.Read(block * blockSize + headerSize, infra::MakeRange(bytes));
            return bytes;
        }

        void UseEveryFreeBlock()
        {
            for (uint32_t block = 2; block != 32; ++block)
                if (State().blocks.State(block) != fs::NorBlockState::inUse)
                    State().blocks.Set(block, fs::NorBlockState::inUse);
        }
    };
}

TEST_F(NorCheckpointWriterTest, writes_the_modified_nodes_into_a_new_node_block_and_the_checkpoint_into_a_new_block)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    EXPECT_EQ(6, State().checkpointBlock);
    EXPECT_EQ((fs::NorCheckpoint{ 5 * blockSize + nodeSlots + nodeSize, 1, 3, 6, firstRecord, 2, 32 }), State().checkpoint);
    EXPECT_EQ((fs::NorMapRoot{ 5 * blockSize + nodeSlots + nodeSize, 1 }), State().inodeTable);
    EXPECT_EQ((fs::NorJournal{ 6, firstRecord, 1 }), State().journal);
    EXPECT_THAT(State().epoch, testing::ElementsAre(6));
    EXPECT_EQ(2, State().liveNodes);
    EXPECT_EQ(5, State().nodeBlock);
    EXPECT_EQ(nodeSlots + 2 * nodeSize, State().nodeOffset);
    EXPECT_EQ(0, Volume().Cache().ModifiedNodes());
    EXPECT_EQ(0, Volume().Cache().ReleasedNodes());
    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::node, 4, {} }), HeaderOf(5).allocation);
    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::checkpoint, 5, {} }), HeaderOf(6).allocation);
}

TEST_F(NorCheckpointWriterTest, frees_the_previous_checkpoint_block_and_the_blocks_of_its_epoch)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));

    WriteCheckpoint();

    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(3));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(4));
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(5));
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(6));
}

TEST_F(NorCheckpointWriterTest, the_checkpoint_on_flash_holds_the_state_and_marks_the_blocks_in_use)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));

    WriteCheckpoint();

    auto bytes = CheckpointOnFlash(6);
    EXPECT_EQ(State().checkpoint, fs::OpenNorCheckpoint(infra::MakeRange(bytes)));
    for (uint32_t block = 0; block != 32; ++block)
        EXPECT_EQ(block < 3 || block == 5 || block == 6, fs::IsNorBlockInUse(infra::MakeRange(bytes), block)) << block;
}

TEST_F(NorCheckpointWriterTest, a_volume_mounts_from_the_checkpoint_with_the_same_contents)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a/b"));
    WriteCheckpoint();
    auto checkpoint = State().checkpoint;
    Unmount();

    Mount();

    EXPECT_EQ(checkpoint, State().checkpoint);
    EXPECT_EQ(fs::EntryType::directory, Stat("/a/b").second.type);
}

TEST_F(NorCheckpointWriterTest, a_second_checkpoint_of_a_mount_continues_the_node_block)
{
    MakeDirectory("/a");
    WriteCheckpoint();
    MakeDirectory("/b");

    WriteCheckpoint();

    EXPECT_EQ(5, State().nodeBlock);
    EXPECT_EQ(nodeSlots + 4 * nodeSize, State().nodeOffset);
    EXPECT_EQ(5 * blockSize + nodeSlots + 3 * nodeSize, State().inodeTable.address);
}

TEST_F(NorCheckpointWriterTest, a_full_node_block_is_followed_by_a_new_one)
{
    for (auto path : { "/a", "/b", "/c", "/d" })
    {
        ASSERT_EQ(fs::Result::success, MakeDirectory(path));
        ASSERT_EQ(fs::Result::success, WriteCheckpoint());
    }

    EXPECT_NE(5, State().nodeBlock);
    Unmount();
    Mount();
    for (auto path : { "/a", "/b", "/c", "/d" })
        EXPECT_EQ(fs::EntryType::directory, Stat(path).second.type) << path;
}

TEST_F(NorCheckpointWriterTest, the_first_checkpoint_after_a_mount_starts_a_new_node_block)
{
    MakeDirectory("/a");
    WriteCheckpoint();
    Unmount();
    Mount();
    MakeDirectory("/b");

    WriteCheckpoint();

    EXPECT_NE(5, State().nodeBlock);
    EXPECT_EQ(nodeSlots + 2 * nodeSize, State().nodeOffset);
}

TEST_F(NorCheckpointWriterTest, a_checkpoint_without_modified_nodes_writes_only_the_checkpoint)
{
    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    EXPECT_EQ(4, State().checkpointBlock);
    EXPECT_EQ(std::nullopt, State().nodeBlock);
    EXPECT_EQ((fs::NorCheckpoint{ 2 * blockSize + nodeSlots, 1, 2, 4, firstRecord, 1, 32 }), State().checkpoint);
}

TEST_F(NorCheckpointWriterTest, a_node_block_without_a_free_block_has_no_space)
{
    MakeDirectory("/a");
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, WriteCheckpoint());
    EXPECT_EQ(3, State().checkpointBlock);
}

TEST_F(NorCheckpointWriterTest, a_checkpoint_block_without_a_free_block_has_no_space)
{
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, WriteCheckpoint());
    EXPECT_EQ(3, State().checkpointBlock);
}

namespace
{
    class NorCheckpointWriterOfThirtyBlocksTest
        : public fs::NorVolumeFixture<config, 30, blockSize>
    {};
}

TEST_F(NorCheckpointWriterOfThirtyBlocksTest, a_checkpoint_of_a_volume_whose_blocks_are_not_a_multiple_of_8_mounts)
{
    Mount();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));
    std::optional<fs::Result> written;
    Volume().Checkpoints().Write([&written](fs::Result result)
        {
            written = result;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, written);
    Unmount();

    Mount();

    EXPECT_EQ(6, State().checkpointBlock);
    EXPECT_EQ(fs::EntryType::directory, Stat("/a").second.type);
}

namespace
{
    constexpr fs::NorConfig largeConfig{ .maxBlocks = 1600, .cachedNodes = 15 };
    constexpr uint32_t largeBlockSize = 1024;
    constexpr std::size_t journalBytes = fs::NorLargestRecordBytes(largeConfig.inlineBytes, largeConfig.maxNameBytes);

    class NorCheckpointWriterOnMockTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorCheckpointWriterOnMockTest()
        {
            auto& state = volume.State();
            state.geometry = fs::MakeNorGeometry(largeConfig, flash);
            state.blocks.Reset(1600);
            state.eraseCounts.Reset(1600);
            for (uint32_t block = 0; block != 1600; ++block)
                state.blocks.Set(block, block % 3 == 0 ? fs::NorBlockState::inUse : fs::NorBlockState::freeNotErased);
            state.blocks.Set(10, fs::NorBlockState::freeErased);
            state.checkpointBlock = 3;
            state.epoch.push_back(3);
            state.journal = fs::NorJournal{ 3, 300, 1 };
            state.inodeTable = fs::NorMapRoot{ 6 * largeBlockSize + 256, 1 };
            state.nextInode = 2;
            state.liveNodes = 1;
            state.nextSequence = 20;
        }

        std::vector<uint8_t> ExpectedCheckpoint() const
        {
            constexpr auto checkpointBytes = fs::NorCheckpointBytes(1600);
            std::vector<uint8_t> bytes(checkpointBytes, 0xff);
            fs::SerializeNorCheckpoint(fs::NorCheckpoint{ 6 * largeBlockSize + 256, 1, 2, 10, 36 + 232, 1, 1600 }, infra::MakeRange(bytes));
            for (uint32_t block = 0; block != 1600; ++block)
                if ((block % 3 == 0 && block != 3) || block == 10)
                    fs::MarkNorBlockInUse(infra::MakeRange(bytes), block);
            fs::SealNorCheckpoint(infra::MakeRange(bytes));
            return bytes;
        }

        void CompleteRequests()
        {
            while (flash.done)
                flash.done();
        }

        testing::StrictMock<hal::FlashMock> flash{ 1600, largeBlockSize };
        fs::DeferredCompletions::WithMaxSize<2> deferred;
        std::array<uint8_t, fs::NorBlockStateBytes(1600)> states{};
        std::array<uint8_t, 1600> counts{};
        std::array<uint8_t, journalBytes> journal{};
        std::array<uint8_t, 15 * 512> nodes{};
        std::array<fs::NorNodeCache::Slot, 15> slots{};
        fs::NorVolume volume{ fs::NorVolumeParts{ flash, deferred, infra::MakeRange(states), infra::MakeRange(counts), infra::MakeRange(journal), infra::MakeRange(nodes), infra::MakeRange(slots) }, largeConfig, std::chrono::milliseconds(10) };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };
}

TEST_F(NorCheckpointWriterOnMockTest, programs_a_large_checkpoint_in_pieces_of_the_journal_buffer_with_its_crc_last)
{
    static_assert(journalBytes == 228);
    auto expected = ExpectedCheckpoint();

    testing::InSequence sequence;
    EXPECT_CALL(flash, writeBufferMock(testing::SizeIs(24), 10 * largeBlockSize + 12));
    EXPECT_CALL(flash, writeBufferMock(std::vector<uint8_t>(expected.begin(), expected.begin() + 228), 10 * largeBlockSize + 36));
    EXPECT_CALL(flash, writeBufferMock(std::vector<uint8_t>(expected.begin() + 228, expected.end()), 10 * largeBlockSize + 36 + 228));
    EXPECT_CALL(done, callback(fs::Result::success));

    volume.Checkpoints().Write([this](fs::Result result)
        {
            done.callback(result);
        });
    CompleteRequests();

    EXPECT_EQ((fs::NorJournal{ 10, 36 + 232, 1 }), volume.State().journal);
}
