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

    class NorRemoveTest
        : public fs::NorFilesFixture<config, 32, 4096>
    {
    public:
        NorRemoveTest()
        {
            Mount();
        }
    };
}

TEST_F(NorRemoveTest, removes_a_file_releases_its_blocks_and_it_stays_removed_after_a_mount)
{
    Unmount();
    auto contents = Pattern<5000>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto first = BlockOf("/a", 0);
    auto second = BlockOf("/a", 1);

    EXPECT_EQ(fs::Result::success, Remove("/a"));

    EXPECT_EQ(fs::Result::notFound, Stat("/a").first);
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(first));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(second));
    Unmount();
    Mount();
    EXPECT_EQ(fs::Result::notFound, Stat("/a").first);
}

TEST_F(NorRemoveTest, removes_an_empty_directory_and_a_file_written_in_this_mount)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    auto file = Open("/d/f", fs::OpenMode::write);
    auto contents = Pattern<5000>();
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    ASSERT_EQ(fs::Result::success, Close(*file));
    file = nullptr;

    EXPECT_EQ(fs::Result::success, Remove("/d/f"));
    EXPECT_EQ(fs::Result::success, Remove("/d"));

    EXPECT_EQ(fs::Result::notFound, Stat("/d").first);
    EXPECT_TRUE(List("/").empty());
}

TEST_F(NorRemoveTest, a_directory_with_entries_is_not_empty)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d/e"));

    EXPECT_EQ(fs::Result::notEmpty, Remove("/d"));
    EXPECT_EQ(fs::EntryType::directory, Stat("/d").second.type);
}

TEST_F(NorRemoveTest, the_root_and_a_relative_path_are_invalid_paths)
{
    EXPECT_EQ(fs::Result::invalidPath, Remove("/"));
    EXPECT_EQ(fs::Result::invalidPath, Remove("a"));
}

TEST_F(NorRemoveTest, a_missing_entry_is_not_found_and_an_entry_below_a_file_is_not_in_a_directory)
{
    auto file = Open("/f", fs::OpenMode::write);
    Close(*file);
    file = nullptr;

    EXPECT_EQ(fs::Result::notFound, Remove("/x"));
    EXPECT_EQ(fs::Result::notFound, Remove("/x/y"));
    EXPECT_EQ(fs::Result::notADirectory, Remove("/f/x"));
}

TEST_F(NorRemoveTest, an_open_file_is_busy_until_its_last_handle_is_closed)
{
    auto created = Open("/f", fs::OpenMode::write);
    Close(*created);
    created = nullptr;
    auto first = Open("/f", fs::OpenMode::read);
    auto second = Open("/f", fs::OpenMode::read);

    EXPECT_EQ(fs::Result::busy, Remove("/f"));
    Close(*first);
    EXPECT_EQ(fs::Result::busy, Remove("/f"));
    Close(*second);
    EXPECT_EQ(fs::Result::success, Remove("/f"));
}

TEST_F(NorRemoveTest, an_open_directory_is_busy_until_its_handle_is_released)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    auto directory = OpenDirectory("/d");
    ASSERT_NE(nullptr, directory);

    EXPECT_EQ(fs::Result::busy, Remove("/d"));
    directory = nullptr;
    EXPECT_EQ(fs::Result::success, Remove("/d"));
}

TEST_F(NorRemoveTest, a_removed_name_can_be_made_again)
{
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d"));
    ASSERT_EQ(fs::Result::success, Remove("/d"));

    EXPECT_EQ(fs::Result::success, MakeDirectory("/d"));
    EXPECT_EQ(fs::EntryType::directory, Stat("/d").second.type);
}

TEST_F(NorRemoveTest, a_remove_whose_data_map_cannot_be_released_is_corrupted)
{
    Unmount();
    auto contents = Pattern<5000>();
    auto file = Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(file).address + 12);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Remove("/a"));
}

TEST_F(NorRemoveTest, an_inode_table_node_with_a_bad_crc_is_corrupted)
{
    Unmount();
    Builder().AddDirectory(root, "d");
    Builder().Commit();
    Builder().Corrupt(Builder().InodeTable().address + 12);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Remove("/d"));
}

namespace
{
    constexpr fs::NorConfig deepConfig{ .maxBlocks = 128, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };
    constexpr uint32_t deepPayload = 4096 - 36;

    class NorRemoveDeepTest
        : public fs::NorFilesFixture<deepConfig, 128, 4096>
    {
    public:
        void PrepareFileAndModifiedNodes()
        {
            PrepareFile();
            ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
            ASSERT_EQ(12, Volume().Cache().ModifiedNodes());
        }

        void PrepareFile()
        {
            static auto contents = Pattern<40 * deepPayload>();
            AddDirectories(root, 60);
            auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
            Builder().Commit();
            ASSERT_EQ(2, Builder().MapOf(file).height);
            Mount();
        }
    };
}

TEST_F(NorRemoveDeepTest, a_remove_that_does_not_fit_next_to_the_modified_nodes_makes_a_checkpoint_first)
{
    PrepareFileAndModifiedNodes();
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Remove("/f"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(fs::Result::notFound, Stat("/f").first);
}

TEST_F(NorRemoveDeepTest, a_remove_whose_release_does_not_fit_next_to_its_paths_makes_a_checkpoint_first)
{
    PrepareFile();
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15 });
    ASSERT_EQ(9, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Remove("/f"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(fs::Result::notFound, Stat("/f").first);
}

TEST_F(NorRemoveDeepTest, a_checkpoint_first_without_a_free_block_has_no_space)
{
    PrepareFileAndModifiedNodes();
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, Remove("/f"));
    EXPECT_EQ(fs::Result::success, Stat("/f").first);
}

TEST_F(NorRemoveDeepTest, a_remove_that_leaves_fewer_than_3_unmodified_nodes_makes_a_checkpoint_after_it)
{
    AddDirectories(root, 60);
    Mount();
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15 });
    ASSERT_EQ(9, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Remove("/d059"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(0, Volume().Cache().ModifiedNodes());
    EXPECT_EQ(fs::Result::notFound, Stat("/d059").first);
}

TEST_F(NorRemoveDeepTest, after_a_checkpoint_that_fails_for_want_of_space_lookups_reads_and_listings_complete_with_no_space)
{
    PrepareFile();
    auto file = Open("/f", fs::OpenMode::read);
    auto directory = OpenDirectory("/");
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, Remove("/d000"));

    EXPECT_EQ(Volume().Cache().Capacity(), Volume().Cache().ModifiedNodes());
    EXPECT_EQ(fs::Result::noSpace, Stat("/d020").first);
    EXPECT_EQ(fs::Result::noSpace, Stat("/d030").first);
    std::array<uint8_t, 16> buffer{};
    EXPECT_EQ(fs::Result::noSpace, Read(*file, infra::MakeRange(buffer)).first);
    std::optional<fs::Result> listed;
    directory->Next([&listed](fs::Result result, std::optional<fs::EntryInfo>)
        {
            listed = result;
        });
    ExecuteAllActions();
    EXPECT_EQ(fs::Result::noSpace, listed);
}

TEST_F(NorRemoveDeepTest, paths_that_do_not_fit_in_the_cache_next_to_the_modified_nodes_make_a_checkpoint_first)
{
    AddDirectories(root, 60);
    Builder().AddDirectory(2, "c");
    Builder().Commit();
    ASSERT_EQ(3, Builder().InodeTable().height);
    Mount();
    ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
    ASSERT_EQ(12, Volume().Cache().ModifiedNodes());
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Remove("/d000/c"));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    EXPECT_EQ(fs::Result::notFound, Stat("/d000/c").first);
}

namespace
{
    constexpr fs::NorConfig longJournal{ .maxBlocks = 32, .cachedNodes = 15, .journalBlocks = 4 };

    class NorRemoveLongJournalTest
        : public fs::NorFilesFixture<longJournal, 32, 4096>
    {
    public:
        NorRemoveLongJournalTest()
        {
            Mount();
        }
    };
}

TEST_F(NorRemoveLongJournalTest, a_block_that_a_record_mapped_and_that_became_a_journal_block_after_a_remove_survives_a_power_loss)
{
    auto file = Open("/x", fs::OpenMode::write);
    auto contents = Pattern<5000>();
    ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    auto reused = static_cast<fs::NorFile&>(*file).State().entries[0].block;
    ASSERT_EQ(fs::Result::success, Close(*file));
    file = nullptr;
    ASSERT_EQ(fs::Result::success, Remove("/x"));
    for (uint32_t block = 2; block != 32; ++block)
        if (block != reused && State().blocks.State(block) != fs::NorBlockState::inUse)
            State().blocks.Set(block, fs::NorBlockState::inUse);
    auto record = Serialized(fs::NorInodeRecord{ 0, fs::NorInodeType::directory });
    for (uint32_t count = 0; State().journal.block != reused && count != 200; ++count)
        ASSERT_EQ(fs::Result::success, Commit({ fs::NorInodePut{ root, infra::MakeRange(record) } }));
    ASSERT_EQ(reused, State().journal.block);
    ASSERT_EQ(fs::Result::success, MakeDirectory("/after"));

    fs::ExposedNorFileSystem<longJournal, 2> afterPowerLoss{ flash };
    std::optional<fs::Result> mounted;
    afterPowerLoss.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    ExecuteAllActions();
    std::optional<fs::Result> found;
    afterPowerLoss.Stat("/after", [&found](fs::Result result, const fs::EntryInfo&)
        {
            found = result;
        });
    ExecuteAllActions();

    EXPECT_EQ(fs::Result::success, mounted);
    EXPECT_EQ(fs::Result::success, found);
    EXPECT_EQ(fs::NorBlockState::inUse, afterPowerLoss.Volume().State().blocks.State(reused));
}
