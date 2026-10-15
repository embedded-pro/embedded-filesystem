#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/test/NorFilesFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    using fs::Bytes;
    using fs::Pattern;

    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    class NorRenameTest
        : public fs::NorFilesFixture<config, 32, 4096>
    {
    public:
        NorRenameTest()
        {
            Mount();
        }

        template<std::size_t Size>
        void BuildFile(const char* name, const std::array<uint8_t, Size>& contents, uint32_t parent = root)
        {
            Unmount();
            Builder().AddFile(parent, name, infra::MakeRange(contents));
            Builder().Commit();
            Mount();
        }
    };
}

TEST_F(NorRenameTest, renames_a_file_and_keeps_its_contents_after_a_mount)
{
    static auto contents = Pattern<5000>();
    BuildFile("a", contents);

    EXPECT_EQ(fs::Result::success, Rename("/a", "/b"));

    EXPECT_EQ(fs::Result::notFound, Stat("/a").first);
    ExpectContents("/b", contents);
    Unmount();
    Mount();
    EXPECT_EQ(fs::Result::notFound, Stat("/a").first);
    ExpectContents("/b", contents);
}

TEST_F(NorRenameTest, a_file_with_bytes_in_its_inline_area_keeps_them)
{
    BuildFile("a", std::array<uint8_t, 5>{ 'h', 'e', 'l', 'l', 'o' });

    EXPECT_EQ(fs::Result::success, Rename("/a", "/b"));

    ExpectContents("/b", std::array<uint8_t, 5>{ 'h', 'e', 'l', 'l', 'o' });
}

TEST_F(NorRenameTest, moves_a_file_and_a_directory_with_its_entries_into_another_directory)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/e"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/e/inner"));
    auto file = Open("/f", fs::OpenMode::write);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("data")));
    ASSERT_EQ(fs::Result::success, Close(*file));
    file = nullptr;

    EXPECT_EQ(fs::Result::success, Rename("/f", "/d/g"));
    EXPECT_EQ(fs::Result::success, Rename("/e", "/d/e"));

    EXPECT_EQ(fs::EntryType::directory, Stat("/d/e/inner").second.type);
    ExpectContents("/d/g", std::array<uint8_t, 4>{ 'd', 'a', 't', 'a' });
    EXPECT_EQ(fs::Result::notFound, Stat("/e").first);
    EXPECT_EQ(fs::Result::notFound, Stat("/f").first);
    Unmount();
    Mount();
    EXPECT_EQ(fs::EntryType::directory, Stat("/d/e/inner").second.type);
}

TEST_F(NorRenameTest, a_moved_directory_can_be_removed_from_its_new_parent)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/e"));
    ASSERT_EQ(fs::Result::success, Rename("/e", "/d/e"));

    EXPECT_EQ(fs::Result::notEmpty, Remove("/d"));
    EXPECT_EQ(fs::Result::success, Remove("/d/e"));
    EXPECT_EQ(fs::Result::success, Remove("/d"));
}

TEST_F(NorRenameTest, a_target_that_exists_already_exists)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/b"));

    EXPECT_EQ(fs::Result::alreadyExists, Rename("/a", "/b"));
    EXPECT_EQ(fs::EntryType::directory, Stat("/a").second.type);
}

TEST_F(NorRenameTest, a_rename_onto_its_own_name_does_nothing)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));
    auto journal = State().journal;

    EXPECT_EQ(fs::Result::success, Rename("/a", "/a"));

    EXPECT_EQ(journal, State().journal);
    EXPECT_EQ(fs::EntryType::directory, Stat("/a").second.type);
}

TEST_F(NorRenameTest, a_name_that_differs_only_in_case_is_another_name)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));

    EXPECT_EQ(fs::Result::success, Rename("/a", "/A"));

    EXPECT_EQ(fs::Result::notFound, Stat("/a").first);
    EXPECT_EQ(fs::EntryType::directory, Stat("/A").second.type);
}

TEST_F(NorRenameTest, the_root_a_target_at_the_root_and_a_directory_into_its_own_subtree_are_invalid_paths)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d/e"));

    EXPECT_EQ(fs::Result::invalidPath, Rename("/", "/x"));
    EXPECT_EQ(fs::Result::invalidPath, Rename("/d", "/"));
    EXPECT_EQ(fs::Result::invalidPath, Rename("/d", "/d/e/f"));
    EXPECT_EQ(fs::Result::invalidPath, Rename("/d", "/d/x"));
    EXPECT_EQ(fs::Result::invalidPath, Rename("/d", "relative"));
    EXPECT_EQ(fs::EntryType::directory, Stat("/d/e").second.type);
}

TEST_F(NorRenameTest, a_directory_may_move_next_to_a_name_that_starts_like_it)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/dx"));

    EXPECT_EQ(fs::Result::success, Rename("/d", "/dx/d"));
}

TEST_F(NorRenameTest, a_missing_source_or_target_parent_is_not_found_and_a_target_below_a_file_is_not_in_a_directory)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));
    auto file = Open("/f", fs::OpenMode::write);
    Close(*file);
    file = nullptr;

    EXPECT_EQ(fs::Result::notFound, Rename("/x", "/y"));
    EXPECT_EQ(fs::Result::notFound, Rename("/x/y", "/z"));
    EXPECT_EQ(fs::Result::notFound, Rename("/a", "/missing/a"));
    EXPECT_EQ(fs::Result::notADirectory, Rename("/a", "/f/a"));
    EXPECT_EQ(fs::Result::notADirectory, Rename("/f/a", "/b"));
    EXPECT_EQ(fs::Result::notADirectory, Rename("/f", "/f/a"));
}

TEST_F(NorRenameTest, an_open_file_or_directory_is_busy)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    auto file = Open("/f", fs::OpenMode::write);
    auto directory = OpenDirectory("/d");

    EXPECT_EQ(fs::Result::busy, Rename("/f", "/g"));
    EXPECT_EQ(fs::Result::busy, Rename("/d", "/e"));
    Close(*file);
    file = nullptr;
    directory = nullptr;
    EXPECT_EQ(fs::Result::success, Rename("/f", "/g"));
    EXPECT_EQ(fs::Result::success, Rename("/d", "/e"));
}

TEST_F(NorRenameTest, a_rename_is_durable_before_it_completes)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/a"));
    ASSERT_EQ(fs::Result::success, Rename("/a", "/b"));

    fs::ExposedNorFileSystem<config, 2> afterPowerLoss{ flash };
    std::optional<fs::Result> mounted;
    afterPowerLoss.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    ExecuteAllActions();
    std::optional<fs::Result> found;
    afterPowerLoss.Stat("/b", [&found](fs::Result result, const fs::EntryInfo&)
        {
            found = result;
        });
    ExecuteAllActions();

    EXPECT_EQ(fs::Result::success, mounted);
    EXPECT_EQ(fs::Result::success, found);
}

TEST_F(NorRenameTest, an_inode_table_node_with_a_bad_crc_is_corrupted)
{
    Unmount();
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Builder().Corrupt(Builder().InodeTable().address + 12);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Rename("/a", "/b"));
}

TEST_F(NorRenameTest, a_target_directory_with_a_bad_crc_is_corrupted)
{
    Unmount();
    auto target = Builder().AddDirectory(root, "t");
    Builder().AddDirectory(target, "x");
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(target).address + 13);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Rename("/a", "/t/a"));
}

TEST_F(NorRenameTest, a_target_hash_whose_256_indexes_are_used_has_no_space)
{
    Unmount();
    auto other = Builder().AddFile(root, "other", Bytes(""));
    Builder().AddDirectory(root, "b");
    for (uint32_t index = 0; index != 256; ++index)
        Builder().AddEntry(root, fs::NorDirectoryKey("a") + index, other);
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::noSpace, Rename("/b", "/a"));
    EXPECT_EQ(fs::EntryType::directory, Stat("/b").second.type);
}

namespace
{
    constexpr fs::NorConfig deepConfig{ .maxBlocks = 128, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };

    class NorRenameDeepTest
        : public fs::NorFilesFixture<deepConfig, 128, 4096>
    {};
}

TEST_F(NorRenameDeepTest, a_rename_within_a_directory_whose_removal_empties_a_leaf_puts_the_new_entry_where_the_room_check_measured_it)
{
    AddDirectories(root, 60);
    auto directory = Builder().AddDirectory(root, "t");
    FillDirectory(directory, 18);
    auto source = Builder().AddDirectory(directory, "s");
    Builder().Commit();
    ASSERT_EQ(2, Builder().MapOf(directory).height);
    ASSERT_LT(fs::NorDirectoryKey("s"), fs::NorDirectoryKey("a"));
    Mount();

    auto& cache = Volume().Cache();
    auto keyOfSource = fs::NorDirectoryKey("s");
    auto unmodifiedOfPaths = [&]
    {
        auto map = RecordInVolume(directory)->mapRoot;
        auto height = RecordInVolume(directory)->mapHeight;
        Fetch(InodeTableId(), State().inodeTable, source);
        Fetch(InodeTableId(), State().inodeTable, directory);
        Fetch(DirectoryId(directory), fs::NorMapRoot{ map, height }, keyOfSource);
        auto unmodified = cache.UnmodifiedPinnedNodes();
        cache.UnpinAll();
        return unmodified;
    };
    for (uint32_t inode = 3; cache.ModifiedNodes() + unmodifiedOfPaths() < cache.Capacity(); inode += 2)
        ModifyLeavesOf({ inode });
    ASSERT_EQ(cache.Capacity(), cache.ModifiedNodes() + unmodifiedOfPaths());

    EXPECT_EQ(fs::Result::success, Rename("/t/s", "/t/a"));

    EXPECT_EQ(fs::Result::notFound, Stat("/t/s").first);
    EXPECT_EQ(fs::EntryType::directory, Stat("/t/a").second.type);
    Unmount();
    Mount();
    EXPECT_EQ(fs::EntryType::directory, Stat("/t/a").second.type);
}

TEST_F(NorRenameDeepTest, a_target_directory_whose_map_would_need_a_fourth_level_has_no_space)
{
    auto target = Builder().AddDirectory(root, "t");
    FillDirectory(target, 18 * 18 * 18);
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::noSpace, Rename("/a", "/t/a"));
}

TEST_F(NorRenameDeepTest, a_rename_that_does_not_fit_next_to_the_modified_nodes_makes_a_checkpoint_first)
{
    AddDirectories(root, 3);
    auto parent = Builder().AddDirectory(root, "p");
    AddDirectories(parent, 18);
    Mount();
    ModifyLeavesOf({ 2, 4, 6 });
    for (uint32_t index = 0; Volume().Cache().ModifiedNodes() < 12; ++index)
        ModifyLeavesOf({ 8 + 2 * index });
    ASSERT_EQ(12, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Rename("/d001", "/p/x"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(fs::EntryType::directory, Stat("/p/x").second.type);
}

TEST_F(NorRenameDeepTest, paths_that_do_not_fit_in_the_cache_next_to_the_modified_nodes_make_a_checkpoint_first)
{
    AddDirectories(root, 60);
    Builder().AddDirectory(2, "c");
    Builder().Commit();
    ASSERT_EQ(3, Builder().InodeTable().height);
    Mount();
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
    ASSERT_EQ(12, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Rename("/d000/c", "/d059/c"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(fs::EntryType::directory, Stat("/d059/c").second.type);
}

TEST_F(NorRenameDeepTest, a_checkpoint_first_without_a_free_block_has_no_space)
{
    AddDirectories(root, 3);
    auto parent = Builder().AddDirectory(root, "p");
    AddDirectories(parent, 18);
    Mount();
    ModifyLeavesOf({ 2, 4, 6 });
    for (uint32_t index = 0; Volume().Cache().ModifiedNodes() < 12; ++index)
        ModifyLeavesOf({ 8 + 2 * index });
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, Rename("/d001", "/p/x"));
    EXPECT_EQ(fs::EntryType::directory, Stat("/d001").second.type);
}

TEST_F(NorRenameDeepTest, a_rename_that_leaves_fewer_than_3_unmodified_nodes_makes_a_checkpoint_after_it)
{
    AddDirectories(root, 60);
    Mount();
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15 });
    ASSERT_EQ(9, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Rename("/d059", "/n"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(0, Volume().Cache().ModifiedNodes());
}
