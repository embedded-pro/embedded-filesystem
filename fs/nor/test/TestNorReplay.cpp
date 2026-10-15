#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/test/NorFilesFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    constexpr fs::NorConfig writerConfig{ .maxBlocks = 64, .cachedNodes = 40, .journalBlocks = 4 };
    constexpr fs::NorConfig readerConfig{ .maxBlocks = 64, .cachedNodes = 15, .journalBlocks = 4 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t recordsPerLeaf = fs::NorKeyedEntries(writerConfig.nodeSize, fs::NorInodeRecordBytes(writerConfig));
    constexpr uint32_t leavesThatFitTheReader = 14;
    constexpr uint32_t headerSize = fs::NorHeaderSize(writerConfig.programUnit);
    constexpr uint32_t partBOffset = fs::NorHeaderPartBOffset(writerConfig.programUnit);

    class NorReplayTest
        : public fs::NorFilesFixture<writerConfig, 64, blockSize>
    {
    public:
        template<class Target>
        void MountWith(Target& target, fs::Result expected = fs::Result::success)
        {
            testing::StrictMock<infra::MockCallback<void(fs::Result)>> mounted;
            target.Mount([&mounted](fs::Result result)
                {
                    mounted.callback(result);
                });
            EXPECT_CALL(mounted, callback(expected));
            ExecuteAllActions();
        }

        template<class Target>
        std::optional<fs::EntryType> TypeIn(Target& target, infra::BoundedConstString path)
        {
            std::optional<fs::EntryType> type;
            target.Stat(path, [&type](fs::Result result, const fs::EntryInfo& info)
                {
                    if (result == fs::Result::success)
                        type = info.type;
                });
            ExecuteAllActions();
            return type;
        }

        template<class Target>
        std::optional<fs::NorInodeRecord> RecordIn(Target& target, uint32_t inode)
        {
            auto& volume = target.Volume();
            std::optional<fs::NorFetchResult> fetched;
            volume.Fetch().Fetch(InodeTableId(), volume.State().inodeTable, inode, [&fetched](fs::NorFetchResult result)
                {
                    fetched = result;
                });
            ExecuteAllActions();
            EXPECT_EQ(fs::NorFetchResult::fetched, fetched);
            auto bytes = fs::NorBTree(volume.Cache(), InodeTableId()).Find(volume.State().inodeTable, inode);
            volume.Cache().UnpinAll();
            if (!bytes.has_value())
                return std::nullopt;

            return fs::ParseNorInodeRecord(*bytes, writerConfig);
        }

        template<class Target>
        void ExpectContentsIn(Target& target, infra::BoundedConstString path)
        {
            infra::SharedPtr<fs::File> file;
            target.Open(path, fs::OpenMode::read, [&file](fs::Result, infra::SharedPtr<fs::File> opened)
                {
                    file = opened;
                });
            ExecuteAllActions();
            ASSERT_NE(nullptr, file);

            std::array<uint8_t, 1000> read{};
            std::optional<std::size_t> bytesRead;
            file->Read(infra::MakeRange(read), [&bytesRead](fs::Result, std::size_t size)
                {
                    bytesRead = size;
                });
            ExecuteAllActions();
            EXPECT_EQ(contents.size(), bytesRead);
            EXPECT_EQ(contents, read);
            file = nullptr;
            ExecuteAllActions();
        }

        infra::BoundedConstString PathOf(uint32_t index)
        {
            paths[index] = { '/', 'd', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
            return infra::BoundedConstString(paths[index].data(), paths[index].size());
        }

        uint32_t MakeDirectoriesUntilTheReplayNeedsMoreThan(std::size_t nodes)
        {
            uint32_t made = 0;
            while (Volume().Cache().ModifiedNodes() <= nodes)
            {
                EXPECT_LT(made, paths.size());
                EXPECT_EQ(fs::Result::success, MakeDirectory(PathOf(made++)));
            }

            return made;
        }

        template<class Target>
        void ExpectDirectories(Target& target, uint32_t made)
        {
            for (uint32_t index = 0; index != made; ++index)
                EXPECT_EQ(fs::EntryType::directory, TypeIn(target, PathOf(index))) << index;
        }

        void WearFreeBlocksExcept(uint32_t kept)
        {
            std::array<uint8_t, fs::norHeaderPartBBytes> partB{};
            for (uint32_t block = 2; block != 64; ++block)
            {
                auto state = State().blocks.State(block);
                if (block == kept || (state != fs::NorBlockState::freeErased && state != fs::NorBlockState::freeNotErased))
                    continue;

                Builder().SetEraseCount(block, 9);
                partB.fill(0xff);
                fs::SerializeNorHeaderB(fs::NorAllocation{ fs::NorBlockType::data, 1000 + block, { 99, 0 } }, infra::MakeRange(partB));
                Builder().Program(block * blockSize + partBOffset, infra::MakeRange(partB));
            }
        }

        void SetUpInodeTable()
        {
            AddDirectories(root, 160);
            Mount();
            std::optional<fs::Result> written;
            Volume().Checkpoints().Write([&written](fs::Result result)
                {
                    written = result;
                });
            ExecuteAllActions();
            ASSERT_EQ(fs::Result::success, written);
        }

        static uint32_t InodeInLeaf(uint32_t leaf)
        {
            return 2 + leaf * recordsPerLeaf;
        }

        void PutInode(uint32_t inode, const char* name = "m")
        {
            auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::directory, 0, 0, 0, {}, infra::StringAsByteRange(infra::BoundedConstString(name)) });
            fs::NorRecordPosition position{ State().journal.block, State().journal.end };
            ASSERT_EQ(fs::Result::success, Commit({ fs::NorInodePut{ inode, infra::MakeRange(record) } }));
            if (State().journal.block != position.block)
                position = fs::NorRecordPosition{ State().journal.block, headerSize };
            positions[committed++] = position;
        }

        void PutLeaves(uint32_t first, uint32_t count)
        {
            for (uint32_t leaf = first; leaf != first + count; ++leaf)
                PutInode(InodeInLeaf(leaf));
        }

        void FillJournalBlock()
        {
            auto block = State().journal.block;
            while (State().journal.end + (State().journal.end - positions[committed - 1].offset) <= blockSize)
                PutInode(InodeInLeaf(0) + 1, "f");
            ASSERT_EQ(block, State().journal.block);
        }

        uint32_t IndexOf(const fs::NorCheckpoint& checkpoint) const
        {
            for (uint32_t index = 0; index != committed; ++index)
                if (positions[index].block == checkpoint.recordsBlock && positions[index].offset == checkpoint.recordsOffset)
                    return index;

            return committed;
        }

        template<class Target>
        void ExpectLeavesPut(Target& target, uint32_t first, uint32_t count)
        {
            for (uint32_t leaf = first; leaf != first + count; ++leaf)
            {
                auto record = RecordIn(target, InodeInLeaf(leaf));
                ASSERT_TRUE(record.has_value()) << leaf;
                EXPECT_TRUE(infra::ContentsEqual(infra::StringAsByteRange(infra::BoundedConstString("m")), record->name)) << leaf;
            }
        }

        template<class Target>
        void Settle(Target& target)
        {
            auto& eraser = target.Volume().Eraser();
            while (eraser.HasWork())
            {
                eraser.Step([] {});
                ExecuteAllActions();
            }

            testing::StrictMock<infra::MockCallback<void(fs::Result)>> unmounted;
            target.Unmount([&unmounted](fs::Result result)
                {
                    unmounted.callback(result);
                });
            EXPECT_CALL(unmounted, callback(fs::Result::success));
            ExecuteAllActions();
        }

        std::array<uint8_t, 1000> contents{ fs::Pattern<1000>() };
        std::array<std::array<char, 4>, 60> paths{};
        std::array<fs::NorRecordPosition, 80> positions{};
        uint32_t committed{ 0 };
        fs::ExposedNorFileSystem<readerConfig> reader{ device };
    };
}

TEST_F(NorReplayTest, a_mount_with_the_cache_of_the_writer_writes_nothing)
{
    Mount();
    auto made = MakeDirectoriesUntilTheReplayNeedsMoreThan(readerConfig.cachedNodes);
    fs::ExposedNorFileSystem<writerConfig> sameCache{ device };
    device.recording = true;

    MountWith(sameCache);

    EXPECT_TRUE(device.requests.empty());
    ExpectDirectories(sameCache, made);
}

TEST_F(NorReplayTest, a_mount_whose_cache_cannot_hold_the_journal_writes_a_checkpoint_whose_records_start_in_the_journal)
{
    Mount();
    auto made = MakeDirectoriesUntilTheReplayNeedsMoreThan(readerConfig.cachedNodes);
    auto writerCheckpoint = State().checkpointBlock;

    MountWith(reader);

    const auto& state = reader.Volume().State();
    EXPECT_NE(writerCheckpoint, state.checkpointBlock);
    EXPECT_NE(state.checkpointBlock, state.checkpoint.recordsBlock);
    ExpectDirectories(reader, made);
}

TEST_F(NorReplayTest, after_the_checkpoint_of_a_replay_and_the_erases_that_follow_both_caches_mount_the_volume)
{
    Mount();
    auto made = MakeDirectoriesUntilTheReplayNeedsMoreThan(readerConfig.cachedNodes);
    MountWith(reader);
    Settle(reader);

    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    MountWith(smallAgain);
    ExpectDirectories(smallAgain, made);

    fs::ExposedNorFileSystem<writerConfig> large{ device };
    MountWith(large);
    ExpectDirectories(large, made);
}

TEST_F(NorReplayTest, the_volume_takes_changes_after_a_mount_that_wrote_the_checkpoint_of_a_replay)
{
    Mount();
    auto made = MakeDirectoriesUntilTheReplayNeedsMoreThan(readerConfig.cachedNodes);
    MountWith(reader);

    testing::StrictMock<infra::MockCallback<void(fs::Result)>> madeNew;
    reader.MakeDirectory("/new", [&madeNew](fs::Result result)
        {
            madeNew.callback(result);
        });
    EXPECT_CALL(madeNew, callback(fs::Result::success));
    ExecuteAllActions();

    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    MountWith(smallAgain);
    ExpectDirectories(smallAgain, made);
    EXPECT_EQ(fs::EntryType::directory, TypeIn(smallAgain, "/new"));
}

TEST_F(NorReplayTest, a_data_block_that_a_record_after_the_checkpoint_sets_is_not_erased_for_the_checkpoint)
{
    Mount();
    MakeDirectoriesUntilTheReplayNeedsMoreThan(readerConfig.cachedNodes);
    auto file = Open("/g", fs::OpenMode::write);
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    ASSERT_EQ(fs::Result::success, Close(*file));
    auto block = BlockOf("/g", 0);
    WearFreeBlocksExcept(block);

    MountWith(reader);

    EXPECT_EQ(fs::NorBlockType::data, HeaderOf(block).allocation->type);
    ExpectContentsIn(reader, "/g");
    Settle(reader);
    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    MountWith(smallAgain);
    ExpectContentsIn(smallAgain, "/g");
}

TEST_F(NorReplayTest, a_replay_that_overflows_in_the_checkpoint_block_keeps_it_for_its_records)
{
    SetUpInodeTable();
    auto checkpointBlock = State().checkpointBlock;
    PutLeaves(0, 20);
    ASSERT_EQ(checkpointBlock, State().journal.block);
    ASSERT_EQ(leavesThatFitTheReader, IndexOf(fs::NorCheckpoint{ 0, 0, 0, checkpointBlock, positions[leavesThatFitTheReader].offset }));

    MountWith(reader);

    const auto& state = reader.Volume().State();
    EXPECT_EQ(checkpointBlock, state.checkpoint.recordsBlock);
    EXPECT_EQ(fs::NorBlockState::inUse, state.blocks.State(checkpointBlock));
    EXPECT_THAT(state.epoch, testing::ElementsAre(checkpointBlock));
    Settle(reader);
    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    MountWith(smallAgain);
    ExpectLeavesPut(smallAgain, 0, 20);
}

TEST_F(NorReplayTest, a_replay_that_overflows_at_the_first_record_of_a_journal_block_skips_its_link_on_the_next_mount)
{
    SetUpInodeTable();
    PutLeaves(0, leavesThatFitTheReader);
    FillJournalBlock();
    PutLeaves(leavesThatFitTheReader, 6);
    auto overflow = positions[committed - 6];
    ASSERT_EQ(headerSize, overflow.offset);

    MountWith(reader);

    const auto& state = reader.Volume().State();
    EXPECT_EQ(overflow.block, state.checkpoint.recordsBlock);
    EXPECT_EQ(headerSize, state.checkpoint.recordsOffset);
    Settle(reader);
    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    MountWith(smallAgain);
    ExpectLeavesPut(smallAgain, 0, leavesThatFitTheReader + 6);
}

TEST_F(NorReplayTest, a_replay_that_overflows_in_a_third_journal_block_frees_the_blocks_before_it)
{
    SetUpInodeTable();
    auto checkpointBlock = State().checkpointBlock;
    PutLeaves(0, 2);
    FillJournalBlock();
    PutInode(InodeInLeaf(0) + 1, "f");
    auto second = State().journal.block;
    FillJournalBlock();
    PutInode(InodeInLeaf(0) + 1, "f");
    auto third = State().journal.block;
    ASSERT_NE(checkpointBlock, second);
    ASSERT_NE(second, third);
    PutLeaves(2, 18);
    ASSERT_EQ(third, State().journal.block);

    MountWith(reader);

    const auto& state = reader.Volume().State();
    EXPECT_EQ(third, state.checkpoint.recordsBlock);
    EXPECT_NE(fs::NorBlockState::inUse, state.blocks.State(checkpointBlock));
    EXPECT_NE(fs::NorBlockState::inUse, state.blocks.State(second));
    EXPECT_THAT(state.epoch, testing::ElementsAre(third));
    EXPECT_EQ(1, state.journal.blocks);
    Settle(reader);
    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    MountWith(smallAgain);
    ExpectLeavesPut(smallAgain, 0, 20);
}

TEST_F(NorReplayTest, a_replay_that_overflows_again_after_its_checkpoint_writes_another_one)
{
    SetUpInodeTable();
    PutLeaves(0, 30);

    MountWith(reader);

    EXPECT_LT(leavesThatFitTheReader, IndexOf(reader.Volume().State().checkpoint));
    Settle(reader);
    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    MountWith(smallAgain);
    ExpectLeavesPut(smallAgain, 0, 30);
}

TEST_F(NorReplayTest, a_first_record_that_does_not_fit_an_empty_cache_makes_the_mount_unsupported_without_writing)
{
    Mount();
    for (uint32_t index = 0; index != 15; ++index)
        ASSERT_EQ(fs::Result::success, MakeDirectory(PathOf(index)));
    std::optional<fs::Result> written;
    Volume().Checkpoints().Write([&written](fs::Result result)
        {
            written = result;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, written);
    ASSERT_EQ(fs::Result::success, Commit({ fs::NorDirectoryEntryPut{ 2, 0, 3 }, fs::NorDirectoryEntryPut{ 3, 0, 3 }, fs::NorDirectoryEntryPut{ 4, 0, 3 }, fs::NorDirectoryEntryPut{ 5, 0, 3 }, fs::NorDirectoryEntryPut{ 6, 0, 3 }, fs::NorDirectoryEntryPut{ 7, 0, 3 }, fs::NorDirectoryEntryPut{ 8, 0, 3 }, fs::NorDirectoryEntryPut{ 9, 0, 3 }, fs::NorDirectoryEntryPut{ 10, 0, 3 }, fs::NorDirectoryEntryPut{ 11, 0, 3 }, fs::NorDirectoryEntryPut{ 12, 0, 3 }, fs::NorDirectoryEntryPut{ 13, 0, 3 }, fs::NorDirectoryEntryPut{ 14, 0, 3 }, fs::NorDirectoryEntryPut{ 15, 0, 3 }, fs::NorDirectoryEntryPut{ 16, 0, 3 } }));
    ASSERT_LT(readerConfig.cachedNodes, Volume().Cache().ModifiedNodes());
    device.recording = true;

    MountWith(reader, fs::Result::unsupported);

    EXPECT_TRUE(device.requests.empty());
}

TEST_F(NorReplayTest, a_checkpoint_of_a_replay_that_finds_every_free_block_mapped_by_a_later_record_fails_the_mount_with_no_space)
{
    static std::array<uint8_t, 35 * (blockSize - headerSize)> filler{};
    AddDirectories(root, 160);
    Builder().AddFile(root, "big", infra::MakeRange(filler));
    auto file = Builder().AddFile(root, "f", infra::StringAsByteRange(infra::BoundedConstString("abc")));
    Builder().Commit();
    MountWith(reader);
    std::array<uint32_t, 64> freeBlocks{};
    uint32_t freeCount = 0;
    for (uint32_t block = 0; block != freeBlocks.size(); ++block)
        if (auto state = reader.Volume().State().blocks.State(block); state == fs::NorBlockState::freeErased || state == fs::NorBlockState::freeNotErased)
            freeBlocks[freeCount++] = block;
    ASSERT_GT(16, freeCount);
    auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::directory, 0, 0, 0, {}, infra::StringAsByteRange(infra::BoundedConstString("m")) });
    for (uint32_t leaf = 0; leaf != 20; ++leaf)
        Builder().AppendRecord({ fs::NorInodePut{ InodeInLeaf(leaf), infra::MakeRange(record) } });
    for (uint32_t index = 0; index != freeCount; ++index)
        Builder().AppendRecord({ fs::NorBlockSet{ file, index, 0, freeBlocks[index] } });
    fs::ExposedNorFileSystem<readerConfig> again{ device };

    MountWith(again, fs::Result::noSpace);
}
