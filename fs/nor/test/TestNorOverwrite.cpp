#include "fs/nor/test/NorFilesFixture.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using fs::Bytes;
    using fs::Pattern;

    constexpr fs::NorConfig config{ .maxBlocks = 48, .cachedNodes = 15 };
    constexpr uint32_t payload = 4096 - 36;
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    template<std::size_t Size>
    std::array<uint8_t, Size> Overwritten(std::array<uint8_t, Size> contents, uint32_t at, infra::ConstByteRange bytes)
    {
        std::ranges::copy(bytes, contents.begin() + at);
        return contents;
    }

    class NorOverwriteTest
        : public fs::NorFilesFixture<config, 48, 4096>
    {
    public:
        NorOverwriteTest()
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

        infra::SharedPtr<fs::File> OpenAt(uint32_t position)
        {
            auto file = Open("/a", fs::OpenMode::readWrite);
            file->Seek(position);
            return file;
        }
    };
}

TEST_F(NorOverwriteTest, an_overwrite_within_a_block_copies_the_block_and_releases_the_old_one_at_the_commit)
{
    static auto contents = Pattern<3 * payload + 100>();
    BuildFile(contents);
    auto previous = BlockOf("/a", 0);
    auto file = OpenAt(100);
    auto bytes = Pattern<50>(9);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(bytes)));

    EXPECT_EQ(150, file->Position());
    EXPECT_EQ(3 * payload + 100, file->Size());
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(previous));
    EXPECT_EQ(fs::Result::success, Close(*file));
    file = nullptr;
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(previous));
    EXPECT_NE(previous, BlockOf("/a", 0));
    static auto expected = Overwritten(contents, 100, infra::MakeRange(bytes));
    ExpectContents("/a", expected);
}

TEST_F(NorOverwriteTest, an_overwrite_across_two_blocks_copies_both)
{
    static auto contents = Pattern<3 * payload + 100>();
    BuildFile(contents);
    auto first = BlockOf("/a", 0);
    auto second = BlockOf("/a", 1);
    auto file = OpenAt(payload - 20);
    auto bytes = Pattern<40>(9);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(bytes)));
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    EXPECT_NE(first, BlockOf("/a", 0));
    EXPECT_NE(second, BlockOf("/a", 1));
    static auto expected = Overwritten(contents, payload - 20, infra::MakeRange(bytes));
    ExpectContents("/a", expected);
}

TEST_F(NorOverwriteTest, an_overwrite_of_buffered_bytes_programs_nothing)
{
    auto file = Open("/a", fs::OpenMode::write);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("0123456789")));
    file->Seek(2);
    device.recording = true;

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("ab")));

    EXPECT_TRUE(device.requests.empty());
    EXPECT_EQ(4, file->Position());
    EXPECT_EQ(fs::Result::success, Close(*file));
    file = nullptr;
    ExpectContents("/a", std::array<uint8_t, 10>{ '0', '1', 'a', 'b', '4', '5', '6', '7', '8', '9' });
}

TEST_F(NorOverwriteTest, an_overwrite_that_runs_past_the_end_appends_the_rest)
{
    static auto contents = Pattern<5000>();
    BuildFile(contents);
    auto file = OpenAt(4990);
    auto bytes = Pattern<30>(9);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(bytes)));

    EXPECT_EQ(5020, file->Size());
    EXPECT_EQ(5020, file->Position());
    EXPECT_EQ(fs::Result::success, Close(*file));
    file = nullptr;
    std::array<uint8_t, 5020> expected{};
    std::ranges::copy(contents, expected.begin());
    std::ranges::copy(bytes, expected.begin() + 4990);
    ExpectContents("/a", expected);
}

TEST_F(NorOverwriteTest, an_overwrite_of_the_last_block_lets_later_appends_continue_in_the_copy)
{
    auto file = Open("/a", fs::OpenMode::write);
    static auto contents = Pattern<5000>();
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    file->Seek(4500);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("xyz")));
    file->Seek(5000);

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("end")));
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    std::array<uint8_t, 5003> expected{};
    std::ranges::copy(contents, expected.begin());
    std::ranges::copy(Bytes("xyz"), expected.begin() + 4500);
    std::ranges::copy(Bytes("end"), expected.begin() + 5000);
    ExpectContents("/a", expected);
}

TEST_F(NorOverwriteTest, a_second_overwrite_of_a_copied_block_copies_it_again_and_frees_the_first_copy)
{
    static auto contents = Pattern<3 * payload + 100>();
    BuildFile(contents);
    auto file = OpenAt(10);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("abc")));
    auto firstCopy = static_cast<fs::NorFile&>(*file).State().entries[0].block;
    file->Seek(20);

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("def")));

    const auto& entries = static_cast<fs::NorFile&>(*file).State().entries;
    ASSERT_EQ(1, entries.size());
    EXPECT_NE(firstCopy, entries[0].block);
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(firstCopy));
    EXPECT_EQ(fs::Result::success, Close(*file));
    file = nullptr;
    static auto once = Overwritten(contents, 10, Bytes("abc"));
    static auto expected = Overwritten(once, 20, Bytes("def"));
    ExpectContents("/a", expected);
}

TEST_F(NorOverwriteTest, reads_through_the_handle_see_the_overwritten_bytes)
{
    static auto contents = Pattern<3 * payload + 100>();
    BuildFile(contents);
    auto file = OpenAt(payload - 2);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("abcd")));
    file->Seek(payload - 4);
    std::array<uint8_t, 8> read{};

    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 8 }), Read(*file, infra::MakeRange(read)));

    EXPECT_EQ((std::array<uint8_t, 8>{ contents[payload - 4], contents[payload - 3], 'a', 'b', 'c', 'd', contents[payload + 2], contents[payload + 3] }), read);
}

TEST_F(NorOverwriteTest, an_overwrite_of_more_blocks_than_a_handle_keeps_commits_on_the_way)
{
    static auto contents = Pattern<12 * payload + 100>();
    BuildFile(contents);
    auto file = OpenAt(10);
    static auto bytes = Pattern<10 * payload>(9);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(bytes)));
    EXPECT_GT(8, static_cast<fs::NorFile&>(*file).State().entries.size());
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    static auto expected = Overwritten(contents, 10, infra::MakeRange(bytes));
    ExpectContents("/a", expected);
}

TEST_F(NorOverwriteTest, an_append_after_overwrites_keeps_room_for_the_copy_of_a_last_block_written_before_the_mount)
{
    static auto contents = Pattern<9 * payload + 100>();
    BuildFile(contents);
    auto file = OpenAt(0);
    for (uint32_t block = 0; block != 8; ++block)
    {
        file->Seek(block * payload);
        ASSERT_EQ(fs::Result::success, Write(*file, Bytes("x"))) << block;
    }
    file->Seek(9 * payload + 100);

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("end")));
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    static auto expected = contents;
    std::array<uint8_t, 9 * payload + 103> withEnd{};
    for (uint32_t block = 0; block != 8; ++block)
        expected[block * payload] = 'x';
    std::ranges::copy(expected, withEnd.begin());
    std::ranges::copy(Bytes("end"), withEnd.begin() + 9 * payload + 100);
    static auto final = withEnd;
    ExpectContents("/a", final);
}

TEST_F(NorOverwriteTest, an_overwrite_without_a_free_block_has_no_space_and_keeps_its_position)
{
    static auto contents = Pattern<5000>();
    BuildFile(contents);
    auto file = OpenAt(10);
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, Write(*file, Bytes("abc")));

    EXPECT_EQ(10, file->Position());
    EXPECT_EQ(5000, file->Size());
}

TEST_F(NorOverwriteTest, an_overwrite_that_needs_a_block_from_the_reserve_first_collects_node_blocks_with_a_block_of_unused_slots)
{
    static auto contents = Pattern<5000>();
    AddDirectoriesWithUnusedNodeSlots();
    BuildFile(contents);
    auto file = OpenAt(10);
    LeaveFreeBlocks(Volume().Geometry().ReserveBlocks());
    ASSERT_TRUE(Volume().Collector().DueForSpace());

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("abc")));

    EXPECT_EQ(fs::Result::success, Close(*file));
    file = nullptr;
    static auto expected = contents;
    std::ranges::copy(Bytes("abc"), expected.begin() + 10);
    ExpectContents("/a", expected);
}

TEST_F(NorOverwriteTest, an_overwrite_whose_block_cannot_be_found_is_corrupted)
{
    static auto contents = Pattern<5000>();
    BuildFile(contents);
    auto inode = InodeOf("/a");
    auto file = OpenAt(10);
    Volume().Cache().Clear();
    Builder().Corrupt(Builder().MapOf(inode).address + 12);
    Volume().Cache().Clear();

    EXPECT_EQ(fs::Result::corrupted, Write(*file, Bytes("abc")));
}

TEST_F(NorOverwriteTest, an_overwrite_whose_record_cannot_be_read_is_corrupted)
{
    static auto contents = Pattern<5000>();
    BuildFile(contents);
    auto file = OpenAt(10);
    Volume().Cache().Clear();
    Builder().Corrupt(Builder().InodeTable().address + 12);

    EXPECT_EQ(fs::Result::corrupted, Write(*file, Bytes("abc")));
    EXPECT_EQ(0, Volume().Cache().UnmodifiedPinnedNodes());
}

TEST_F(NorOverwriteTest, an_overwrite_that_cannot_commit_the_blocks_it_holds_completes_with_the_result_of_the_commit)
{
    static auto contents = Pattern<10 * payload>();
    BuildFile(contents);
    auto file = OpenAt(0);
    for (uint32_t block = 0; block != 8; ++block)
    {
        file->Seek(block * payload);
        ASSERT_EQ(fs::Result::success, Write(*file, Bytes("x"))) << block;
    }
    UseEveryFreeBlock();
    file->Seek(8 * payload);

    EXPECT_EQ(fs::Result::noSpace, Write(*file, Bytes("x")));
}

TEST_F(NorOverwriteTest, an_overwrite_in_write_mode_after_a_seek_back_works_like_in_read_write_mode)
{
    auto file = Open("/a", fs::OpenMode::write);
    static auto contents = Pattern<5000>();
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    file->Seek(0);

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("start")));
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    static auto expected = Overwritten(contents, 0, Bytes("start"));
    ExpectContents("/a", expected);
}

namespace
{
    constexpr fs::NorConfig wideUnitConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    class NorOverwriteWideUnitTest
        : public fs::NorFilesFixture<wideUnitConfig, 48, 2048>
    {
    public:
        NorOverwriteWideUnitTest()
        {
            Mount();
        }
    };
}

TEST_F(NorOverwriteWideUnitTest, an_overwrite_that_ends_in_the_inline_area_changes_the_inline_bytes)
{
    Unmount();
    static auto contents = Pattern<3000>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::readWrite);
    file->Seek(2990);

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("0123456789")));
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    Unmount();
    Mount();
    static auto expected = Overwritten(contents, 2990, Bytes("0123456789"));
    ExpectContents("/a", expected);
}

namespace
{
    constexpr fs::NorConfig deepConfig{ .maxBlocks = 128, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };
    constexpr uint32_t deepPayload = 4096 - 36;

    class NorOverwriteDeepTest
        : public fs::NorFilesFixture<deepConfig, 128, 4096>
    {
    public:
        template<std::size_t Size>
        infra::SharedPtr<fs::File> PrepareFile(const std::array<uint8_t, Size>& contents)
        {
            AddDirectories(root, 60);
            Builder().AddFile(root, "f", infra::MakeRange(contents));
            Builder().Commit();
            Mount();
            return Open("/f", fs::OpenMode::readWrite);
        }

        std::vector<uint32_t> UncommittedIndices(fs::File& file) const
        {
            std::vector<uint32_t> indices;
            for (const auto& entry : static_cast<fs::NorFile&>(file).State().entries)
                indices.push_back(entry.index);

            return indices;
        }

        fs::Result OverwriteBlock(fs::File& file, uint32_t index)
        {
            file.Seek(index * deepPayload + 7);
            return Write(file, Bytes("x"));
        }
    };
}

TEST_F(NorOverwriteDeepTest, an_overwrite_in_a_third_leaf_of_the_data_map_commits_the_first_two_leaves_first)
{
    static auto contents = Pattern<81 * deepPayload>();
    auto file = PrepareFile(contents);
    ASSERT_EQ(fs::Result::success, OverwriteBlock(*file, 0));
    ASSERT_EQ(fs::Result::success, OverwriteBlock(*file, 40));
    ASSERT_EQ((std::vector<uint32_t>{ 0, 40 }), UncommittedIndices(*file));

    EXPECT_EQ(fs::Result::success, OverwriteBlock(*file, 75));

    EXPECT_EQ((std::vector<uint32_t>{ 75 }), UncommittedIndices(*file));
}

TEST_F(NorOverwriteDeepTest, a_new_block_in_a_third_leaf_commits_the_overwrites_first)
{
    static auto contents = Pattern<81 * deepPayload>();
    auto file = PrepareFile(contents);
    ASSERT_EQ(fs::Result::success, OverwriteBlock(*file, 0));
    ASSERT_EQ(fs::Result::success, OverwriteBlock(*file, 40));
    file->Seek(81 * deepPayload);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(Pattern<100>(3))));

    EXPECT_EQ((std::vector<uint32_t>{ 81 }), UncommittedIndices(*file));
}

TEST_F(NorOverwriteDeepTest, a_commit_of_overwrites_in_two_leaves_fits_its_changes_into_the_cache)
{
    static auto contents = Pattern<81 * deepPayload>();
    auto file = PrepareFile(contents);
    ASSERT_EQ(fs::Result::success, OverwriteBlock(*file, 0));
    ASSERT_EQ(fs::Result::success, OverwriteBlock(*file, 40));
    ASSERT_EQ(fs::Result::success, OverwriteBlock(*file, 1));
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
    ASSERT_EQ(12, Volume().Cache().ModifiedNodes());

    EXPECT_EQ(fs::Result::success, Sync(*file));
    EXPECT_EQ(fs::Result::success, Close(*file));

    file = nullptr;
    static auto expected = contents;
    for (auto index : { 0u, 40u, 1u })
        expected[index * deepPayload + 7] = 'x';
    ExpectContents("/f", expected);
}
