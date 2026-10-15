#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/test/NorRecordsFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <limits>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    infra::ConstByteRange Bytes(const char* text)
    {
        return infra::StringAsByteRange(infra::BoundedConstString(text));
    }

    class NorRecordApplyTest
        : public fs::NorRecordsFixture<config, 32, 4096>
    {
    public:
        Record Directory(const char* name, uint32_t parent = root, uint32_t mapRoot = 0, uint8_t height = 0)
        {
            return Serialized(fs::NorInodeRecord{ parent, fs::NorInodeType::directory, height, 0, mapRoot, {}, Bytes(name) });
        }

        Record File(const char* name, uint32_t parent = root)
        {
            return Serialized(fs::NorInodeRecord{ parent, fs::NorInodeType::file, 0, 0, 0, {}, Bytes(name) });
        }

        void AddNumberedDirectories(uint32_t count)
        {
            for (uint32_t index = 0; index != count; ++index)
            {
                std::array<char, 3> name{ 'd', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
                Builder().AddDirectory(root, infra::BoundedConstString(name.data(), name.size()));
            }
        }

        void FillCacheWithModifiedNodes(uint32_t highestDirectory, std::size_t leftFree = 0)
        {
            auto record = Directory("x");
            for (uint32_t inode = highestDirectory; Volume().Cache().ModifiedNodes() < Volume().Cache().Capacity() - leftFree; inode -= 3)
                ASSERT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ inode, infra::MakeRange(record) } })) << inode;
            ASSERT_EQ(Volume().Cache().Capacity() - leftFree, Volume().Cache().ModifiedNodes());
        }
    };
}

TEST_F(NorRecordApplyTest, an_inode_put_of_a_new_inode_inserts_it_without_a_map_and_raises_the_next_inode)
{
    Mount();
    auto record = Directory("docs", root, 0x4100, 2);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ 9, infra::MakeRange(record) } }));

    auto applied = RecordInVolume(9);
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(fs::NorInodeType::directory, applied->type);
    EXPECT_EQ(0, applied->mapRoot);
    EXPECT_EQ(0, applied->mapHeight);
    EXPECT_TRUE(infra::ContentsEqual(Bytes("docs"), applied->name));
    EXPECT_EQ(10, State().nextInode);
}

TEST_F(NorRecordApplyTest, an_inode_put_below_the_next_inode_keeps_the_next_inode)
{
    Mount();
    auto record = File("a");
    Apply({ fs::NorInodePut{ 9, infra::MakeRange(record) } });

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ 5, infra::MakeRange(record) } }));

    EXPECT_EQ(10, State().nextInode);
}

TEST_F(NorRecordApplyTest, an_inode_put_of_an_existing_inode_keeps_its_map)
{
    auto docs = Builder().AddDirectory(root, "docs");
    Builder().AddDirectory(docs, "inner");
    Builder().Commit();
    auto map = Builder().MapOf(docs);
    Mount();
    auto record = Directory("renamed");

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ docs, infra::MakeRange(record) } }));

    auto applied = RecordInVolume(docs);
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(map.address, applied->mapRoot);
    EXPECT_EQ(map.height, applied->mapHeight);
    EXPECT_TRUE(infra::ContentsEqual(Bytes("renamed"), applied->name));
}

TEST_F(NorRecordApplyTest, an_inode_put_that_changes_the_type_is_corrupted)
{
    auto docs = Builder().AddDirectory(root, "docs");
    Builder().Commit();
    Mount();
    auto record = File("docs");

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodePut{ docs, infra::MakeRange(record) } }));
}

TEST_F(NorRecordApplyTest, an_inode_put_of_inode_0_or_of_a_record_that_does_not_parse_is_corrupted)
{
    Mount();
    auto record = File("a");
    auto unknownType = File("b");
    unknownType[fs::norInodeTypeOffset] = 7;

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodePut{ 0, infra::MakeRange(record) } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodePut{ 9, infra::MakeRange(unknownType) } }));
}

TEST_F(NorRecordApplyTest, an_inode_put_of_the_last_inode_number_leaves_no_number)
{
    Mount();
    auto record = File("a");

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ std::numeric_limits<uint32_t>::max(), infra::MakeRange(record) } }));

    EXPECT_EQ(std::numeric_limits<uint32_t>::max(), State().nextInode);
}

TEST_F(NorRecordApplyTest, a_directory_entry_put_adds_the_entry_and_puts_the_new_root_into_the_record_of_the_directory)
{
    Mount();
    auto record = File("a");

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ 2, infra::MakeRange(record) }, fs::NorDirectoryEntryPut{ root, fs::NorDirectoryKey("a"), 2 } }));

    EXPECT_EQ(2, EntryInVolume(root, fs::NorDirectoryKey("a")));
    auto rootRecord = RecordInVolume(root);
    ASSERT_TRUE(rootRecord.has_value());
    EXPECT_EQ(1, rootRecord->mapHeight);
    EXPECT_NE(0, rootRecord->mapRoot & fs::norModifiedNodeTag);
}

TEST_F(NorRecordApplyTest, a_directory_entry_put_into_a_missing_directory_or_a_file_is_corrupted)
{
    auto file = Builder().AddFile(root, "f", Bytes("x"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryPut{ 9, 0x100, file } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryPut{ file, 0x100, root } }));
}

TEST_F(NorRecordApplyTest, a_directory_entry_put_of_a_key_that_exists_is_corrupted)
{
    auto file = Builder().AddFile(root, "f", Bytes("x"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryPut{ root, fs::NorDirectoryKey("f"), file } }));
}

TEST_F(NorRecordApplyTest, a_link_first_in_a_linked_record_is_skipped)
{
    Mount();
    auto record = File("a");

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorLink{ 3, 72 }, fs::NorInodePut{ 2, infra::MakeRange(record) } }, true));

    EXPECT_TRUE(RecordInVolume(2).has_value());
}

TEST_F(NorRecordApplyTest, a_link_that_does_not_start_a_linked_record_is_corrupted)
{
    Mount();
    auto record = File("a");

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorLink{ 3, 72 } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorLink{ 3, 72 }, fs::NorLink{ 3, 72 } }, true));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodePut{ 2, infra::MakeRange(record) }, fs::NorLink{ 3, 72 } }, true));
}

TEST_F(NorRecordApplyTest, a_directory_entry_remove_removes_the_entry_and_puts_the_new_root_into_the_record_of_the_directory)
{
    Builder().AddDirectory(root, "a");
    Builder().AddDirectory(root, "b");
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorDirectoryEntryRemove{ root, fs::NorDirectoryKey("a") } }));

    EXPECT_EQ(std::nullopt, EntryInVolume(root, fs::NorDirectoryKey("a")));
    EXPECT_NE(std::nullopt, EntryInVolume(root, fs::NorDirectoryKey("b")));
    EXPECT_EQ(fs::Result::success, Apply({ fs::NorDirectoryEntryRemove{ root, fs::NorDirectoryKey("b") } }));
    EXPECT_EQ(0, RecordInVolume(root)->mapRoot);
    EXPECT_EQ(0, RecordInVolume(root)->mapHeight);
}

TEST_F(NorRecordApplyTest, a_directory_entry_remove_of_a_missing_key_from_a_missing_directory_or_from_a_file_is_corrupted)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryRemove{ root, fs::NorDirectoryKey("g") } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryRemove{ 7, fs::NorDirectoryKey("f") } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryRemove{ file, fs::NorDirectoryKey("f") } }));
}

TEST_F(NorRecordApplyTest, an_inode_remove_of_a_file_releases_its_blocks_and_the_nodes_of_its_map)
{
    std::array<uint8_t, 5000> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto first = BlockInVolume(file, 0);
    auto second = BlockInVolume(file, 1);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorDirectoryEntryRemove{ root, fs::NorDirectoryKey("f") }, fs::NorInodeRemove{ file } }));

    EXPECT_EQ(std::nullopt, RecordInVolume(file));
    EXPECT_EQ(std::nullopt, EntryInVolume(root, fs::NorDirectoryKey("f")));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(first));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(second));
    EXPECT_EQ(3, Volume().Cache().ReleasedNodes());
}

TEST_F(NorRecordApplyTest, an_inode_remove_of_an_empty_directory_removes_it)
{
    auto docs = Builder().AddDirectory(root, "docs");
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorDirectoryEntryRemove{ root, fs::NorDirectoryKey("docs") }, fs::NorInodeRemove{ docs } }));

    EXPECT_EQ(std::nullopt, RecordInVolume(docs));
}

TEST_F(NorRecordApplyTest, an_inode_remove_of_the_root_of_a_directory_with_entries_or_of_a_missing_inode_is_corrupted)
{
    auto docs = Builder().AddDirectory(root, "docs");
    Builder().AddDirectory(docs, "inner");
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodeRemove{ root } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodeRemove{ docs } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodeRemove{ 9 } }));
}

TEST_F(NorRecordApplyTest, a_block_set_maps_the_block_puts_the_new_root_into_the_record_and_marks_the_block_in_use)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorBlockSet{ file, 0, 0, 9 } }));

    EXPECT_EQ(9, BlockInVolume(file, 0));
    EXPECT_EQ(1, RecordInVolume(file)->mapHeight);
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(9));
}

TEST_F(NorRecordApplyTest, a_block_set_releases_the_previous_block)
{
    std::array<uint8_t, 5000> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto previous = BlockInVolume(file, 0);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorBlockSet{ file, 0, previous, 20 } }));

    EXPECT_EQ(20, BlockInVolume(file, 0));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(previous));
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(20));
}

TEST_F(NorRecordApplyTest, a_block_set_that_expects_another_previous_block_is_corrupted)
{
    std::array<uint8_t, 5000> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ file, 0, 0, 20 } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ file, 1, 7, 20 } }));
}

TEST_F(NorRecordApplyTest, a_block_set_of_a_block_in_use_or_outside_the_volume_is_corrupted)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ file, 0, 0, 1 } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ file, 0, 0, 2 } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ file, 0, 0, 32 } }));
}

TEST_F(NorRecordApplyTest, a_block_set_of_a_directory_or_of_a_missing_inode_is_corrupted)
{
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ root, 0, 0, 9 } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ 7, 0, 0, 9 } }));
}

TEST_F(NorRecordApplyTest, a_block_set_to_block_0_clears_the_index_and_releases_the_previous_block)
{
    std::array<uint8_t, 5000> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto previous = BlockInVolume(file, 1);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorBlockSet{ file, 1, previous, 0 } }));

    EXPECT_EQ(0, BlockInVolume(file, 1));
    EXPECT_NE(0, BlockInVolume(file, 0));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(previous));
}

TEST_F(NorRecordApplyTest, a_released_block_that_an_uncommitted_checkpoint_took_for_nodes_is_no_longer_a_node_block)
{
    std::array<uint8_t, 5000> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto previous = BlockInVolume(file, 1);
    Unmount();
    Builder().EraseBlock(previous);
    Builder().SetEraseCount(previous, 2);
    Builder().Allocate(previous, fs::NorBlockType::node);
    Mount();
    ASSERT_TRUE(State().nodeBlocks.Contains(previous));

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorBlockSet{ file, 1, previous, 0 } }));

    EXPECT_FALSE(State().nodeBlocks.Contains(previous));
}

TEST_F(NorRecordApplyTest, a_block_set_from_block_0_to_block_0_changes_nothing)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorBlockSet{ file, 0, 0, 0 } }));

    EXPECT_EQ(0, RecordInVolume(file)->mapRoot);
}

TEST_F(NorRecordApplyTest, a_block_set_beyond_the_reach_of_three_levels_is_corrupted)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ file, 124 * 124 * 124, 0, 9 } }));
}

TEST_F(NorRecordApplyTest, a_size_sets_the_size_and_the_inline_bytes)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorSize{ file, 5, Bytes("abcde") } }));

    auto record = RecordInVolume(file);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(5, record->size);
    EXPECT_TRUE(infra::ContentsEqual(Bytes("abcde"), record->inlineBytes));
}

TEST_F(NorRecordApplyTest, a_size_below_the_size_of_the_file_releases_the_blocks_beyond_it)
{
    static std::array<uint8_t, 3 * 4060 + 100> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    std::array<uint32_t, 4> blocks{};
    for (uint32_t index = 0; index != 4; ++index)
        blocks[index] = BlockInVolume(file, index);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorSize{ file, 4070, {} } }));

    EXPECT_EQ(4070, RecordInVolume(file)->size);
    EXPECT_EQ(blocks[0], BlockInVolume(file, 0));
    EXPECT_EQ(blocks[1], BlockInVolume(file, 1));
    EXPECT_EQ(0, BlockInVolume(file, 2));
    EXPECT_EQ(0, BlockInVolume(file, 3));
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(blocks[1]));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(blocks[2]));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(blocks[3]));
}

TEST_F(NorRecordApplyTest, a_size_of_0_releases_every_block_and_the_map)
{
    static std::array<uint8_t, 20 * 4060> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    std::array<uint32_t, 20> blocks{};
    for (uint32_t index = 0; index != 20; ++index)
        blocks[index] = BlockInVolume(file, index);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorSize{ file, 0, {} } }));

    auto record = RecordInVolume(file);
    EXPECT_EQ(0, record->size);
    EXPECT_EQ(0, record->mapRoot);
    EXPECT_EQ(0, record->mapHeight);
    for (auto block : blocks)
        EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(block)) << block;
}

TEST_F(NorRecordApplyTest, a_release_through_a_data_node_with_a_bad_crc_or_of_a_block_outside_the_volume_is_corrupted)
{
    std::array<uint8_t, 5000> contents{};
    auto damaged = Builder().AddFile(root, "a", infra::MakeRange(contents));
    auto outside = Builder().AddFile(root, "b", infra::MakeRange(contents));
    Builder().MapBlock(outside, 1, 40);
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(damaged).address + 12);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorSize{ damaged, 0, {} } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorSize{ outside, 0, {} } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorBlockSet{ outside, 1, 40, 9 } }));
}

TEST_F(NorRecordApplyTest, a_smaller_size_within_the_inline_area_only_changes_the_inline_bytes)
{
    auto file = Builder().AddFile(root, "f", Bytes("abcdef"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorSize{ file, 2, Bytes("ab") } }));

    EXPECT_EQ(2, RecordInVolume(file)->size);
    EXPECT_TRUE(infra::ContentsEqual(Bytes("ab"), RecordInVolume(file)->inlineBytes));
}

TEST_F(NorRecordApplyTest, a_size_with_more_inline_bytes_than_the_inline_area_or_the_size_is_corrupted)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();
    std::array<uint8_t, 65> tooMany{};

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorSize{ file, 100, infra::MakeRange(tooMany) } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorSize{ file, 3, Bytes("abcd") } }));
}

TEST_F(NorRecordApplyTest, a_size_of_a_directory_or_of_a_missing_inode_is_corrupted)
{
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorSize{ root, 0, {} } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorSize{ 7, 0, {} } }));
}

TEST_F(NorRecordApplyTest, an_empty_record_changes_nothing)
{
    Mount();

    EXPECT_EQ(fs::Result::success, Apply({}));
    EXPECT_EQ(0, Volume().Cache().ModifiedNodes());
}

TEST_F(NorRecordApplyTest, a_malformed_record_is_corrupted)
{
    Mount();
    fs::CrcRecord record{ infra::MakeRange(recordBuffer) };
    record.Add(3)[0] = 99;

    EXPECT_EQ(fs::Result::corrupted, ApplyBytes(record.Seal()));
}

TEST_F(NorRecordApplyTest, an_inode_table_node_with_a_bad_crc_is_corrupted)
{
    Builder().AddDirectory(root, "docs");
    Builder().Commit();
    Builder().Corrupt(Builder().InodeTable().address + 20);
    Mount();
    auto record = File("a");

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodePut{ 9, infra::MakeRange(record) } }));
    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryPut{ root, 0x100, 9 } }));
}

TEST_F(NorRecordApplyTest, a_directory_node_with_a_bad_crc_is_corrupted)
{
    Builder().AddDirectory(root, "docs");
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(root).address + 13);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryPut{ root, 0x100, 9 } }));
}

TEST_F(NorRecordApplyTest, a_path_that_does_not_fit_in_the_cache_is_unsupported)
{
    for (uint32_t index = 0; index != 47; ++index)
    {
        std::array<char, 3> name{ 'd', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
        Builder().AddDirectory(root, infra::BoundedConstString(name.data(), name.size()));
    }
    Builder().Commit();
    Mount();

    auto record = Directory("x");
    for (uint32_t inode = 2; inode < 44; inode += 3)
        ASSERT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ inode, infra::MakeRange(record) } })) << inode;
    ASSERT_EQ(15, Volume().Cache().ModifiedNodes());

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorInodePut{ 47, infra::MakeRange(record) } }));
}

TEST_F(NorRecordApplyTest, an_inode_put_whose_split_the_cache_has_no_room_for_is_unsupported)
{
    AddNumberedDirectories(46);
    Builder().Commit();
    Mount();
    FillCacheWithModifiedNodes(47);

    auto record = Directory("x");
    fs::NorBTree inodes{ Volume().Cache(), InodeTableId() };
    uint32_t inode = 48;
    while (inodes.NodesModifiedByInsert(State().inodeTable, inode) == inodes.NodesModifiedByChange(State().inodeTable, inode))
        ASSERT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ inode++, infra::MakeRange(record) } }));
    ASSERT_LT(48, inode);

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorInodePut{ inode, infra::MakeRange(record) } }));
}

TEST_F(NorRecordApplyTest, an_inode_put_into_an_unmodified_leaf_with_room_fits_a_cache_with_room_for_that_leaf)
{
    AddNumberedDirectories(46);
    Builder().Commit();
    Mount();
    FillCacheWithModifiedNodes(44, 1);
    auto record = Directory("x");

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ 48, infra::MakeRange(record) } }));
    EXPECT_EQ(Volume().Cache().Capacity(), Volume().Cache().ModifiedNodes());
}

TEST_F(NorRecordApplyTest, a_directory_entry_put_into_an_unmodified_leaf_with_room_fits_a_cache_with_room_for_that_leaf)
{
    AddNumberedDirectories(60);
    auto directory = Builder().AddDirectory(root, "d");
    Builder().AddDirectory(directory, "e");
    Builder().Commit();
    Mount();
    FillCacheWithModifiedNodes(60, 2);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorDirectoryEntryPut{ directory, 1, 3 } }));
    EXPECT_EQ(Volume().Cache().Capacity(), Volume().Cache().ModifiedNodes());
}

TEST_F(NorRecordApplyTest, a_directory_entry_put_whose_split_the_cache_has_no_room_for_is_unsupported)
{
    AddNumberedDirectories(60);
    Builder().Commit();
    Mount();
    ASSERT_EQ(fs::Result::success, Apply({ fs::NorDirectoryEntryPut{ 2, 0, 3 } }));
    FillCacheWithModifiedNodes(60);

    fs::NorBTree entries{ Volume().Cache(), DirectoryId(2) };
    auto map = [this]
    {
        auto record = RecordInVolume(2);
        return fs::NorMapRoot{ record->mapRoot, record->mapHeight };
    };
    uint32_t key = 1;
    while (entries.NodesModifiedByInsert(map(), key) == entries.NodesModifiedByChange(map(), key))
        ASSERT_EQ(fs::Result::success, Apply({ fs::NorDirectoryEntryPut{ 2, key++, 3 } }));
    ASSERT_LT(1, key);

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorDirectoryEntryPut{ 2, key, 3 } }));
}

TEST_F(NorRecordApplyTest, a_block_set_whose_new_map_node_the_cache_has_no_room_for_is_unsupported)
{
    AddNumberedDirectories(60);
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();
    auto record = File("f");
    ASSERT_EQ(fs::Result::success, Apply({ fs::NorInodePut{ file, infra::MakeRange(record) } }));
    FillCacheWithModifiedNodes(60);

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorBlockSet{ file, 0, 0, 9 } }));
}

TEST_F(NorRecordApplyTest, a_block_set_counts_the_unmodified_node_of_its_inode_as_taken)
{
    AddNumberedDirectories(60);
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();
    FillCacheWithModifiedNodes(60, 1);

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorBlockSet{ file, 0, 0, 9 } }));
}

TEST_F(NorRecordApplyTest, a_block_set_fits_a_cache_with_room_for_the_node_of_its_inode_and_its_new_map_node)
{
    AddNumberedDirectories(60);
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();
    FillCacheWithModifiedNodes(60, 2);

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorBlockSet{ file, 0, 0, 9 } }));
    EXPECT_EQ(Volume().Cache().Capacity(), Volume().Cache().ModifiedNodes());
}

namespace
{
    constexpr fs::NorConfig smallNodes{ .maxBlocks = 128, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };

    class NorRecordApplyDeepTest
        : public fs::NorRecordsFixture<smallNodes, 128, 4096>
    {};
}

TEST_F(NorRecordApplyDeepTest, an_inode_put_that_would_need_a_fourth_level_is_corrupted)
{
    FillInodeTable(648);
    Mount();
    auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::file });

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorInodePut{ 649, infra::MakeRange(record) } }));
}

TEST_F(NorRecordApplyDeepTest, a_directory_entry_put_that_would_need_a_fourth_level_is_corrupted)
{
    FillDirectory(root, 18 * 18 * 18);
    Mount();

    EXPECT_EQ(fs::Result::corrupted, Apply({ fs::NorDirectoryEntryPut{ root, 0x10000, 2 } }));
}

TEST_F(NorRecordApplyDeepTest, a_size_that_leaves_one_block_of_a_two_level_map_lowers_the_map_and_releases_its_nodes)
{
    static std::array<uint8_t, 40 * 4060> contents{};
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().Commit();
    auto map = Builder().MapOf(file);
    ASSERT_EQ(2, map.height);
    Mount();
    auto first = BlockInVolume(file, 0);
    auto last = BlockInVolume(file, 39);
    Volume().Cache().Clear();

    EXPECT_EQ(fs::Result::success, Apply({ fs::NorSize{ file, 100, {} } }));

    auto record = RecordInVolume(file);
    EXPECT_EQ(1, record->mapHeight);
    EXPECT_EQ(first, BlockInVolume(file, 0));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(last));
    EXPECT_EQ(4, Volume().Cache().ReleasedNodes());
}
