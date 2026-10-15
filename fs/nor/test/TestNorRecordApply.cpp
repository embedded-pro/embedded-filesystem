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

TEST_F(NorRecordApplyTest, entries_of_later_steps_are_unsupported)
{
    Mount();

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorInodeRemove{ 2 } }));
    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorDirectoryEntryRemove{ root, 0x100 } }));
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

TEST_F(NorRecordApplyTest, a_block_set_that_clears_an_index_is_unsupported_until_truncation)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorBlockSet{ file, 0, 0, 0 } }));
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

TEST_F(NorRecordApplyTest, a_size_below_the_size_of_the_file_is_unsupported_until_truncation)
{
    auto file = Builder().AddFile(root, "f", Bytes("abc"));
    Builder().Commit();
    Mount();

    EXPECT_EQ(fs::Result::unsupported, Apply({ fs::NorSize{ file, 2, Bytes("ab") } }));
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
