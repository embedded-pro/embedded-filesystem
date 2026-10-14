#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/test/NorRecordsFixture.hpp"
#include "fs/nor/test/NorVolumeFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    infra::ConstByteRange Bytes(const char* text)
    {
        return infra::StringAsByteRange(infra::BoundedConstString(text));
    }

    class NorMakeDirectoryTest
        : public fs::NorVolumeFixture<config, 32, 4096>
    {
    public:
        void ExpectDirectory(infra::BoundedConstString path)
        {
            auto found = Stat(path);
            EXPECT_EQ(fs::Result::success, found.first) << path.data();
            EXPECT_EQ(fs::EntryType::directory, found.second.type) << path.data();
        }

        uint32_t ChildOfInodeTable(uint32_t inode)
        {
            std::array<uint8_t, 512> node{};
            image.Read(Builder().InodeTable().address, infra::MakeRange(node));
            return fs::NorBTree::ChildFor(infra::MakeRange(node), inode);
        }
    };
}

TEST_F(NorMakeDirectoryTest, makes_a_directory_in_the_root)
{
    Mount();

    EXPECT_EQ(fs::Result::success, MakeDirectory("/docs"));

    ExpectDirectory("/docs");
    EXPECT_THAT(List("/"), testing::ElementsAre("docs"));
    EXPECT_TRUE(List("/docs").empty());
}

TEST_F(NorMakeDirectoryTest, makes_a_directory_inside_another)
{
    Builder().AddDirectory(root, "docs");
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, MakeDirectory("/docs/notes"));
    EXPECT_EQ(fs::Result::success, MakeDirectory("/docs/notes/old"));

    ExpectDirectory("/docs/notes/old");
    EXPECT_THAT(List("/docs"), testing::ElementsAre("notes"));
}

TEST_F(NorMakeDirectoryTest, makes_many_directories_through_splits_journal_blocks_and_checkpoints)
{
    Mount();

    for (uint32_t index = 0; index != 60; ++index)
    {
        std::array<char, 3> name{ '/', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
        ASSERT_EQ(fs::Result::success, MakeDirectory(infra::BoundedConstString(name.data(), name.size()))) << index;
    }

    EXPECT_EQ(60, List("/").size());
    EXPECT_EQ(62, State().nextInode);
    ExpectDirectory("/f9");
}

TEST_F(NorMakeDirectoryTest, the_root_already_exists)
{
    Mount();

    EXPECT_EQ(fs::Result::alreadyExists, MakeDirectory("/"));
}

TEST_F(NorMakeDirectoryTest, a_name_that_exists_already_exists)
{
    Builder().AddFile(root, "a", Bytes("abc"));
    Builder().AddDirectory(root, "b");
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::alreadyExists, MakeDirectory("/a"));
    EXPECT_EQ(fs::Result::alreadyExists, MakeDirectory("/b"));
    EXPECT_EQ(fs::Result::success, MakeDirectory("/c"));
    EXPECT_EQ(fs::Result::alreadyExists, MakeDirectory("/c"));
}

TEST_F(NorMakeDirectoryTest, a_missing_parent_is_not_found)
{
    Mount();

    EXPECT_EQ(fs::Result::notFound, MakeDirectory("/x/y"));
}

TEST_F(NorMakeDirectoryTest, a_file_as_parent_is_not_a_directory)
{
    Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::notADirectory, MakeDirectory("/f/y"));
    EXPECT_EQ(fs::Result::notADirectory, MakeDirectory("/f/y/z"));
}

TEST_F(NorMakeDirectoryTest, paths_and_names_are_checked_first)
{
    Mount();

    EXPECT_EQ(fs::Result::invalidPath, MakeDirectory("a"));
    EXPECT_EQ(fs::Result::nameTooLong, MakeDirectory("/0123456789012345678901234567890123456789012345678901234567890123456789"));
}

TEST_F(NorMakeDirectoryTest, names_with_the_same_hash_take_the_lowest_free_index)
{
    Builder().AddFile(root, "t15311", Bytes("1"));
    Builder().AddEntry(root, fs::NorDirectoryKey("t15311") + 2, 2);
    Builder().AddEntry(root, fs::NorDirectoryKey("t15311") + 3, 2);
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, MakeDirectory("/t498781"));
    EXPECT_EQ(fs::Result::success, MakeDirectory("/t636020"));

    EXPECT_THAT(List("/"), testing::ElementsAre("t15311", "t498781", "t15311", "t15311", "t636020"));
}

TEST_F(NorMakeDirectoryTest, a_hash_whose_256_indexes_are_used_has_no_space)
{
    auto other = Builder().AddFile(root, "other", Bytes(""));
    for (uint32_t index = 0; index != 256; ++index)
        Builder().AddEntry(root, fs::NorDirectoryKey("a") + index, other);
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::noSpace, MakeDirectory("/a"));
}

TEST_F(NorMakeDirectoryTest, the_last_inode_number_is_not_given_out)
{
    Builder().AddRecord(std::numeric_limits<uint32_t>::max() - 1, fs::NorInodeRecord{ root, fs::NorInodeType::directory });
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::noSpace, MakeDirectory("/a"));
}

TEST_F(NorMakeDirectoryTest, an_entry_without_a_record_under_the_name_is_corrupted)
{
    Builder().AddEntry(root, fs::NorDirectoryKey("a"), 9);
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, MakeDirectory("/a"));
}

TEST_F(NorMakeDirectoryTest, a_corrupted_node_on_the_path_of_the_new_inode_is_corrupted)
{
    for (auto name : { "a", "b", "c", "d", "e" })
        Builder().AddDirectory(root, name);
    Builder().Commit();
    Builder().Corrupt(ChildOfInodeTable(7) + 20);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, MakeDirectory("/x"));
}

TEST_F(NorMakeDirectoryTest, completes_only_through_the_event_dispatcher)
{
    Mount();

    std::optional<fs::Result> made;
    fileSystem.MakeDirectory("/", [&made](fs::Result result)
        {
            made = result;
        });
    EXPECT_FALSE(made.has_value());

    ExecuteAllActions();
    EXPECT_EQ(fs::Result::alreadyExists, made);
}

TEST_F(NorMakeDirectoryTest, directories_are_there_after_unmount_and_mount)
{
    Mount();
    EXPECT_EQ(fs::Result::success, MakeDirectory("/a"));
    EXPECT_EQ(fs::Result::success, MakeDirectory("/a/b"));
    Unmount();

    Mount();

    ExpectDirectory("/a/b");
    EXPECT_EQ(4, State().nextInode);
}

TEST_F(NorMakeDirectoryTest, directories_made_over_several_mounts_are_replayed)
{
    for (auto path : { "/a", "/b", "/c", "/d", "/e" })
    {
        Mount();
        EXPECT_EQ(fs::Result::success, MakeDirectory(path));
        Unmount();
    }

    Mount();

    EXPECT_THAT(List("/"), testing::UnorderedElementsAre("a", "b", "c", "d", "e"));
}

TEST_F(NorMakeDirectoryTest, a_volume_without_a_free_block_for_the_record_has_no_space)
{
    Mount();
    for (uint32_t block = 2; block != 32; ++block)
        if (State().blocks.State(block) != fs::NorBlockState::inUse)
            State().blocks.Set(block, fs::NorBlockState::inUse);

    EXPECT_EQ(fs::Result::noSpace, MakeDirectory("/a"));
}

namespace
{
    constexpr fs::NorConfig smallNodes{ .maxBlocks = 128, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };

    class NorMakeDirectoryDeepTest
        : public fs::NorRecordsFixture<smallNodes, 128, 4096>
    {
    public:
        using Name = infra::BoundedString::WithStorage<8>;

        static Name NameOf(uint32_t index)
        {
            Name name("d");
            name.push_back(static_cast<char>('0' + index / 100));
            name.push_back(static_cast<char>('0' + index / 10 % 10));
            name.push_back(static_cast<char>('0' + index % 10));
            return name;
        }

        void AddDirectories(uint32_t parent, uint32_t count)
        {
            for (uint32_t index = 0; index != count; ++index)
            {
                Builder().AddDirectory(parent, NameOf(index));
                if (index % 30 == 29)
                    Builder().Commit();
            }
            Builder().Commit();
        }

        void ModifyLeavesOf(std::initializer_list<uint32_t> inodes)
        {
            for (auto inode : inodes)
            {
                auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::directory, 0, 0, 0, {}, Bytes("m") });
                ASSERT_EQ(fs::Result::success, Commit({ fs::NorInodePut{ inode, infra::MakeRange(record) } })) << inode;
            }
        }
    };
}

TEST_F(NorMakeDirectoryDeepTest, an_inode_table_that_would_need_a_fourth_level_has_no_space)
{
    FillInodeTable(648);
    ASSERT_EQ(3, Builder().InodeTable().height);
    Mount();

    EXPECT_EQ(fs::Result::noSpace, MakeDirectory("/a"));
}

TEST_F(NorMakeDirectoryDeepTest, a_directory_whose_map_would_need_a_fourth_level_has_no_space)
{
    FillDirectory(root, 18 * 18 * 18);
    ASSERT_EQ(3, Builder().MapOf(root).height);
    Mount();

    EXPECT_EQ(fs::Result::noSpace, MakeDirectory("/a"));
}

TEST_F(NorMakeDirectoryDeepTest, paths_that_do_not_fit_in_the_cache_next_to_the_modified_nodes_make_a_checkpoint_first)
{
    AddDirectories(root, 60);
    Builder().AddDirectory(2, "c");
    Builder().Commit();
    ASSERT_EQ(3, Builder().InodeTable().height);
    Mount();
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
    ASSERT_EQ(12, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, MakeDirectory("/d000/x"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(fs::EntryType::directory, Stat("/d000/x").second.type);
}

TEST_F(NorMakeDirectoryDeepTest, changes_that_do_not_fit_next_to_the_modified_nodes_make_a_checkpoint_first)
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

    EXPECT_EQ(fs::Result::success, MakeDirectory("/p/x"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(fs::EntryType::directory, Stat("/p/x").second.type);
}

TEST_F(NorMakeDirectoryDeepTest, a_checkpoint_first_without_a_free_block_has_no_space)
{
    AddDirectories(root, 3);
    auto parent = Builder().AddDirectory(root, "p");
    AddDirectories(parent, 18);
    Mount();
    ModifyLeavesOf({ 2, 4, 6 });
    for (uint32_t index = 0; Volume().Cache().ModifiedNodes() < 12; ++index)
        ModifyLeavesOf({ 8 + 2 * index });
    for (uint32_t block = 2; block != 128; ++block)
        if (State().blocks.State(block) != fs::NorBlockState::inUse)
            State().blocks.Set(block, fs::NorBlockState::inUse);

    EXPECT_EQ(fs::Result::noSpace, MakeDirectory("/p/x"));
    EXPECT_EQ(fs::Result::notFound, Stat("/p/x").first);
}
