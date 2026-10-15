#include "fs/nor/test/NorFilesFixture.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
    using fs::Bytes;
    using fs::Pattern;

    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t payload = 4096 - 36;
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    template<std::size_t Size, std::size_t From>
    std::array<uint8_t, Size> Prefix(const std::array<uint8_t, From>& from)
    {
        std::array<uint8_t, Size> prefix{};
        std::copy_n(from.begin(), Size, prefix.begin());
        return prefix;
    }

    class NorTruncateTest
        : public fs::NorFilesFixture<config, 32, 4096>
    {
    public:
        NorTruncateTest()
        {
            Mount();
        }

        template<std::size_t Size>
        void BuildFile(const std::array<uint8_t, Size>& contents)
        {
            Unmount();
            Builder().AddFile(root, "a", infra::MakeRange(contents));
            Builder().Commit();
            Mount();
        }
    };
}

TEST_F(NorTruncateTest, cuts_a_file_written_before_the_mount_at_the_position_and_releases_the_blocks_beyond)
{
    static auto contents = Pattern<3 * payload + 100>();
    BuildFile(contents);
    auto third = BlockOf("/a", 2);
    auto fourth = BlockOf("/a", 3);
    auto file = Open("/a", fs::OpenMode::readWrite);
    file->Seek(payload + 50);

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_EQ(payload + 50, file->Size());
    EXPECT_EQ(payload + 50, file->Position());
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(third));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(fourth));
    Close(*file);
    file = nullptr;
    Unmount();
    Mount();
    ExpectContents("/a", Prefix<payload + 50>(contents));
}

TEST_F(NorTruncateTest, a_truncation_is_durable_before_it_completes)
{
    static auto contents = Pattern<3 * payload + 100>();
    BuildFile(contents);
    auto file = Open("/a", fs::OpenMode::readWrite);
    file->Seek(10);
    ASSERT_EQ(fs::Result::success, Truncate(*file));

    fs::ExposedNorFileSystem<config, 2> afterPowerLoss{ flash };
    std::optional<fs::Result> mounted;
    afterPowerLoss.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    ExecuteAllActions();
    std::optional<uint32_t> size;
    afterPowerLoss.Stat("/a", [&size](fs::Result, const fs::EntryInfo& info)
        {
            size = info.size;
        });
    ExecuteAllActions();

    EXPECT_EQ(fs::Result::success, mounted);
    EXPECT_EQ(10, size);
}

TEST_F(NorTruncateTest, a_truncation_to_0_releases_every_block)
{
    static auto contents = Pattern<2 * payload + 100>();
    BuildFile(contents);
    auto first = BlockOf("/a", 0);
    auto inode = InodeOf("/a");
    auto file = Open("/a", fs::OpenMode::readWrite);

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_EQ(0, file->Size());
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(first));
    EXPECT_EQ(0, RecordInVolume(inode)->mapRoot);
}

TEST_F(NorTruncateTest, a_truncation_to_0_frees_the_uncommitted_blocks_of_the_handle)
{
    auto file = Open("/a", fs::OpenMode::write);
    static auto contents = Pattern<3 * payload>();
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    auto entries = static_cast<fs::NorFile&>(*file).State().entries;
    ASSERT_EQ(3, entries.size());
    file->Seek(0);

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    for (const auto& entry : entries)
        EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(entry.block));
    EXPECT_EQ(0, Stat("/a").second.size);
}

TEST_F(NorTruncateTest, a_truncation_within_the_buffer_keeps_the_bytes_before_the_position)
{
    auto file = Open("/a", fs::OpenMode::write);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("0123456789")));
    file->Seek(4);

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_EQ(4, Stat("/a").second.size);
    Close(*file);
    file = nullptr;
    ExpectContents("/a", Prefix<4>(std::array<uint8_t, 10>{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' }));
}

TEST_F(NorTruncateTest, an_append_after_a_truncation_below_the_programmed_part_copies_the_block)
{
    static auto contents = Pattern<5000>();
    BuildFile(contents);
    auto previous = BlockOf("/a", 1);
    auto file = Open("/a", fs::OpenMode::readWrite);
    file->Seek(4500);
    ASSERT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("abc")));
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    EXPECT_NE(previous, BlockOf("/a", 1));
    std::array<uint8_t, 4503> expected{};
    std::copy_n(contents.begin(), 4500, expected.begin());
    std::ranges::copy(Bytes("abc"), expected.begin() + 4500);
    ExpectContents("/a", expected);
}

TEST_F(NorTruncateTest, a_truncation_at_the_end_commits_what_is_buffered)
{
    auto file = Open("/a", fs::OpenMode::write);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("abc")));

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_EQ(3, Stat("/a").second.size);
}

TEST_F(NorTruncateTest, open_in_write_mode_empties_an_existing_file_before_it_completes)
{
    static auto contents = Pattern<2 * payload + 100>();
    BuildFile(contents);
    auto first = BlockOf("/a", 0);

    auto file = Open("/a", fs::OpenMode::write);

    ASSERT_NE(nullptr, file);
    EXPECT_EQ(0, file->Size());
    EXPECT_EQ(0, Stat("/a").second.size);
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(first));
    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("new")));
    EXPECT_EQ(fs::Result::success, Close(*file));
    file = nullptr;
    ExpectContents("/a", std::array<uint8_t, 3>{ 'n', 'e', 'w' });
}

TEST_F(NorTruncateTest, open_in_write_mode_that_cannot_empty_the_file_hands_out_no_handle)
{
    static auto contents = Pattern<2 * payload + 100>();
    BuildFile(contents);
    auto inode = InodeOf("/a");
    Builder().Corrupt(Builder().MapOf(inode).address + 12);
    Volume().Cache().Clear();

    Open("/a", fs::OpenMode::write, fs::Result::corrupted);
}

TEST_F(NorTruncateTest, a_truncation_whose_record_cannot_be_read_is_corrupted)
{
    static auto contents = Pattern<5000>();
    BuildFile(contents);
    auto file = Open("/a", fs::OpenMode::readWrite);
    Volume().Cache().Clear();
    Builder().Corrupt(Builder().InodeTable().address + 12);
    file->Seek(100);

    EXPECT_EQ(fs::Result::corrupted, Truncate(*file));
    EXPECT_EQ(0, Volume().Cache().UnmodifiedPinnedNodes());
}

TEST_F(NorTruncateTest, a_truncation_below_programmed_bytes_of_uncommitted_blocks_commits_them_first)
{
    auto file = Open("/a", fs::OpenMode::write);
    static auto contents = Pattern<3 * payload>();
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    auto first = static_cast<fs::NorFile&>(*file).State().entries[0].block;
    file->Seek(payload + 100);

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_TRUE(static_cast<fs::NorFile&>(*file).State().entries.empty());
    Close(*file);
    file = nullptr;
    EXPECT_EQ(first, BlockOf("/a", 0));
    ExpectContents("/a", Prefix<payload + 100>(contents));
}

TEST_F(NorTruncateTest, a_truncation_whose_first_commit_fails_completes_with_its_result)
{
    static auto contents = Pattern<payload + 100>();
    BuildFile(contents);
    auto file = Open("/a", fs::OpenMode::append);
    static auto appended = Pattern<2 * payload>(5);
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(appended)));
    ASSERT_EQ(0, Volume().Cache().ModifiedNodes());
    Volume().Cache().Clear();
    Builder().Corrupt(Builder().InodeTable().address + 12);
    file->Seek(payload + 100);

    EXPECT_EQ(fs::Result::corrupted, Truncate(*file));
}

TEST_F(NorTruncateTest, a_tail_block_that_cannot_be_found_is_corrupted)
{
    static auto contents = Pattern<5000>();
    BuildFile(contents);
    auto inode = InodeOf("/a");
    auto file = Open("/a", fs::OpenMode::readWrite);
    Volume().Cache().Clear();
    Builder().Corrupt(Builder().MapOf(inode).address + 12);
    Volume().Cache().Clear();
    file->Seek(100);

    EXPECT_EQ(fs::Result::corrupted, Truncate(*file));
}

namespace
{
    constexpr fs::NorConfig wideUnitConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };
    constexpr uint32_t widePayload = 2048 - fs::NorHeaderSize(16);

    class NorTruncateWideUnitTest
        : public fs::NorFilesFixture<wideUnitConfig, 48, 2048>
    {
    public:
        NorTruncateWideUnitTest()
        {
            Mount();
        }
    };
}

TEST_F(NorTruncateWideUnitTest, a_truncation_keeps_the_bytes_after_the_last_whole_program_unit_in_the_inline_area)
{
    Unmount();
    static auto contents = Pattern<widePayload + 500>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto inode = InodeOf("/a");
    auto file = Open("/a", fs::OpenMode::readWrite);
    file->Seek(widePayload + 304 + 7);

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_EQ(7, RecordInVolume(inode)->inlineBytes.size());
    Close(*file);
    file = nullptr;
    Unmount();
    Mount();
    ExpectContents("/a", Prefix<widePayload + 311>(contents));
}

namespace
{
    constexpr fs::NorConfig deepConfig{ .maxBlocks = 128, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };
    constexpr uint32_t deepPayload = 4096 - 36;

    class NorTruncateDeepTest
        : public fs::NorFilesFixture<deepConfig, 128, 4096>
    {};
}

TEST_F(NorTruncateDeepTest, a_truncation_whose_release_does_not_fit_next_to_the_modified_nodes_makes_a_checkpoint_first)
{
    static auto contents = Pattern<40 * deepPayload>();
    AddDirectories(root, 60);
    Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto file = Open("/f", fs::OpenMode::readWrite);
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
    ASSERT_EQ(12, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;
    file->Seek(100);

    EXPECT_EQ(fs::Result::success, Truncate(*file));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(100, Stat("/f").second.size);
}
