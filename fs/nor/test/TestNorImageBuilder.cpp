#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "gmock/gmock.h"
#include <array>
#include <optional>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t firstRecord = headerSize + 36;
    constexpr uint32_t inodeRecordBytes = fs::NorInodeRecordBytes(config);

    class NorImageBuilderTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        NorImageBuilderTest()
        {
            fs::NorFormatter::WithStorage<config> formatter{ flash };
            formatter.Format([](fs::Result) {});
            ExecuteAllActions();
        }

        fs::NorBlockHeader Header(uint32_t block) const
        {
            std::array<uint8_t, headerSize> header{};
            image.Read(block * blockSize, infra::MakeRange(header));
            return fs::ParseNorBlockHeader(infra::MakeRange(header), 1);
        }

        std::optional<fs::NorCheckpoint> CheckpointIn(uint32_t block) const
        {
            image.Read(block * blockSize + headerSize, infra::MakeRange(checkpointBytes));
            return fs::OpenNorCheckpoint(infra::MakeRange(checkpointBytes));
        }

        std::optional<fs::OpenedRecord> RecordAt(uint32_t address) const
        {
            image.Read(address, infra::MakeRange(recordBytes));
            return fs::OpenRecord(infra::MakeRange(recordBytes));
        }

        hal::FlashStub flash{ 32, blockSize };
        fs::NorFlashStubImage image{ flash };
        mutable std::array<uint8_t, 36> checkpointBytes{};
        mutable std::array<uint8_t, 256> recordBytes{};
        std::optional<fs::NorImageBuilder> builder;

        fs::NorImageBuilder& Builder()
        {
            if (!builder.has_value())
                builder.emplace(image, config);

            return *builder;
        }
    };
}

TEST_F(NorImageBuilderTest, starts_from_the_volume_that_the_formatter_writes)
{
    auto& built = Builder();

    EXPECT_EQ(32, built.Geometry().blocks);
    EXPECT_EQ(3, built.CheckpointBlock());
    EXPECT_EQ(3, built.JournalBlock());
    EXPECT_EQ(firstRecord, built.JournalEnd());
    EXPECT_EQ(3, built.NextSequence());
}

TEST_F(NorImageBuilderTest, the_image_reads_programs_and_erases_the_flash)
{
    EXPECT_EQ(32, image.Sectors());
    EXPECT_EQ(blockSize, image.SectorSize());

    std::array<uint8_t, 2> data{ 0x0f, 0xf0 };
    image.EraseSector(9);
    image.Program(9 * blockSize + 10, infra::MakeRange(data));
    image.Program(9 * blockSize + 10, infra::MakeRange(std::array<uint8_t, 1>{ 0x3c }));

    std::array<uint8_t, 3> read{};
    image.Read(9 * blockSize + 10, infra::MakeRange(read));
    EXPECT_EQ((std::array<uint8_t, 3>{ 0x0c, 0xf0, 0xff }), read);

    image.EraseSector(9);
    image.Read(9 * blockSize + 10, infra::MakeRange(read));
    EXPECT_EQ((std::array<uint8_t, 3>{ 0xff, 0xff, 0xff }), read);
}

TEST_F(NorImageBuilderTest, set_erase_count_leaves_a_free_erased_block_with_that_count)
{
    Builder().Allocate(6, fs::NorBlockType::data, { 4, 0 });

    Builder().SetEraseCount(6, 41);

    EXPECT_EQ(41, Header(6).eraseCount);
    EXPECT_TRUE(Header(6).IsFreeAndErased());
}

TEST_F(NorImageBuilderTest, erase_block_leaves_a_block_without_part_a)
{
    Builder().EraseBlock(6);

    EXPECT_EQ(std::nullopt, Header(6).eraseCount);
    EXPECT_FALSE(Header(6).IsFreeAndErased());
}

TEST_F(NorImageBuilderTest, allocate_writes_part_b_with_the_next_sequence)
{
    EXPECT_EQ(3, Builder().Allocate(6, fs::NorBlockType::data, { 4, 2 }));
    EXPECT_EQ(4, Builder().Allocate(7, fs::NorBlockType::node));

    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::data, 3, { 4, 2 } }), Header(6).allocation);
    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::node, 4, {} }), Header(7).allocation);
    EXPECT_EQ(5, Builder().NextSequence());
}

TEST_F(NorImageBuilderTest, a_new_checkpoint_takes_the_place_of_the_previous_one)
{
    auto previous = Builder().Checkpoint();

    Builder().WriteCheckpoint(5);

    EXPECT_EQ(fs::NorBlockType::checkpoint, Header(5).allocation->type);
    EXPECT_EQ(3, Header(5).allocation->sequence);
    auto checkpoint = CheckpointIn(5);
    ASSERT_TRUE(checkpoint.has_value());
    EXPECT_EQ(previous.inodeTableRoot, checkpoint->inodeTableRoot);
    EXPECT_EQ(5, checkpoint->recordsBlock);
    EXPECT_EQ(firstRecord, checkpoint->recordsOffset);
    EXPECT_TRUE(fs::IsNorBlockInUse(infra::MakeRange(checkpointBytes), 5));
    EXPECT_FALSE(fs::IsNorBlockInUse(infra::MakeRange(checkpointBytes), 3));
    EXPECT_TRUE(fs::IsNorBlockInUse(infra::MakeRange(checkpointBytes), 2));
    EXPECT_EQ(5, Builder().CheckpointBlock());
    EXPECT_EQ(5, Builder().JournalBlock());
    EXPECT_EQ(firstRecord, Builder().JournalEnd());
}

TEST_F(NorImageBuilderTest, writes_a_checkpoint_as_given)
{
    auto checkpoint = Builder().Checkpoint();
    checkpoint.liveNodes = 9;

    Builder().WriteCheckpoint(5, checkpoint, { 0, 1, 2, 7 });

    ASSERT_TRUE(CheckpointIn(5).has_value());
    EXPECT_EQ(9, CheckpointIn(5)->liveNodes);
    EXPECT_TRUE(fs::IsNorBlockInUse(infra::MakeRange(checkpointBytes), 7));
    EXPECT_FALSE(fs::IsNorBlockInUse(infra::MakeRange(checkpointBytes), 3));
    EXPECT_EQ(3, Builder().CheckpointBlock());
}

TEST_F(NorImageBuilderTest, appends_records_after_the_checkpoint)
{
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    Builder().AppendRecord({ fs::NorDirectoryEntryRemove{ 1, 0x100 } });

    auto first = RecordAt(3 * blockSize + firstRecord);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(1, first->entries);
    auto second = RecordAt(3 * blockSize + firstRecord + first->size);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(firstRecord + first->size + second->size, Builder().JournalEnd());
}

TEST_F(NorImageBuilderTest, the_first_record_of_a_journal_block_links_it_to_the_end_of_the_previous_one)
{
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    auto end = Builder().JournalEnd();

    Builder().StartJournalBlock(8);
    Builder().AppendRecord({ fs::NorInodeRemove{ 8 } });

    EXPECT_EQ(fs::NorBlockType::journal, Header(8).allocation->type);
    auto record = RecordAt(8 * blockSize + headerSize);
    ASSERT_TRUE(record.has_value());
    fs::NorEntryReader reader{ *record, inodeRecordBytes };
    EXPECT_EQ(fs::NorEntry(fs::NorLink{ 3, end }), reader.Next());
    EXPECT_EQ(fs::NorEntry(fs::NorInodeRemove{ 8 }), reader.Next());
    EXPECT_EQ(8, Builder().JournalBlock());
}

TEST_F(NorImageBuilderTest, writes_a_record_where_it_is_told)
{
    auto end = Builder().AppendRecord(9, headerSize, { fs::NorInodeRemove{ 7 } });

    auto record = RecordAt(9 * blockSize + headerSize);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(headerSize + record->size, end);
    EXPECT_EQ(firstRecord, Builder().JournalEnd());
}

TEST_F(NorImageBuilderTest, a_record_is_padded_to_the_program_unit)
{
    hal::FlashStub wideFlash{ 48, 2048 };
    fs::NorFlashStubImage wideImage{ wideFlash };
    constexpr fs::NorConfig wideConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };
    fs::NorFormatter::WithStorage<wideConfig> formatter{ wideFlash };
    formatter.Format([](fs::Result) {});
    ExecuteAllActions();
    fs::NorImageBuilder wideBuilder{ wideImage, wideConfig };
    auto start = wideBuilder.JournalEnd();

    wideBuilder.AppendRecord({ fs::NorInodeRemove{ 7 } });

    EXPECT_EQ(start + 16, wideBuilder.JournalEnd());
}

TEST_F(NorImageBuilderTest, corrupt_clears_a_byte)
{
    Builder().Corrupt(3 * blockSize + headerSize + 1);

    EXPECT_EQ(std::nullopt, CheckpointIn(3));
}

TEST_F(NorImageBuilderTest, a_directory_is_an_inode_record_and_an_entry_in_its_parent)
{
    auto docs = Builder().AddDirectory(fs::NorImageBuilder::rootInode, "docs");

    EXPECT_EQ(2u, docs);
    auto record = Builder().RecordOf(docs);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(fs::NorImageBuilder::rootInode, record->parent);
    EXPECT_EQ(fs::NorInodeType::directory, record->type);
    EXPECT_TRUE(infra::ContentsEqual(infra::StringAsByteRange(infra::BoundedConstString("docs")), record->name));
    EXPECT_EQ(std::optional<uint32_t>(docs), Builder().EntryOf(fs::NorImageBuilder::rootInode, 0x572bb700));
    EXPECT_EQ(1, Builder().MapOf(fs::NorImageBuilder::rootInode).height);
}

TEST_F(NorImageBuilderTest, a_file_keeps_its_contents_in_its_inline_area)
{
    std::array<uint8_t, 5> contents{ 1, 2, 3, 4, 5 };

    auto file = Builder().AddFile(fs::NorImageBuilder::rootInode, "a", infra::MakeRange(contents));

    auto record = Builder().RecordOf(file);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(fs::NorInodeType::file, record->type);
    EXPECT_EQ(5u, record->size);
    EXPECT_TRUE(infra::ContentsEqual(infra::MakeRange(contents), record->inlineBytes));
}

TEST_F(NorImageBuilderTest, names_with_the_same_hash_take_the_next_collision_index)
{
    auto first = Builder().AddDirectory(fs::NorImageBuilder::rootInode, "t15311");
    auto second = Builder().AddDirectory(fs::NorImageBuilder::rootInode, "t498781");

    EXPECT_EQ(std::optional<uint32_t>(first), Builder().EntryOf(fs::NorImageBuilder::rootInode, 0x6b2b9000));
    EXPECT_EQ(std::optional<uint32_t>(second), Builder().EntryOf(fs::NorImageBuilder::rootInode, 0x6b2b9001));
}

TEST_F(NorImageBuilderTest, records_and_entries_are_written_as_given)
{
    Builder().AddRecord(9, fs::NorInodeRecord{ 5, fs::NorInodeType::file, 0, 3 });
    Builder().AddEntry(fs::NorImageBuilder::rootInode, 0x100, 12);

    EXPECT_EQ(5u, Builder().RecordOf(9).value_or(fs::NorInodeRecord{}).parent);
    EXPECT_EQ(std::optional<uint32_t>(12), Builder().EntryOf(fs::NorImageBuilder::rootInode, 0x100));
    EXPECT_EQ(std::nullopt, Builder().RecordOf(10));
    EXPECT_EQ(std::nullopt, Builder().EntryOf(fs::NorImageBuilder::rootInode, 0x101));
}

TEST_F(NorImageBuilderTest, a_record_written_again_replaces_the_previous_one)
{
    Builder().AddRecord(fs::NorImageBuilder::rootInode, fs::NorInodeRecord{ 0, fs::NorInodeType::file, 0, 7 });

    EXPECT_EQ(fs::NorInodeType::file, Builder().RecordOf(fs::NorImageBuilder::rootInode).value_or(fs::NorInodeRecord{}).type);
    EXPECT_EQ(7u, Builder().RecordOf(fs::NorImageBuilder::rootInode).value_or(fs::NorInodeRecord{}).size);
}

TEST_F(NorImageBuilderTest, commit_writes_the_maps_into_a_node_block_and_a_new_checkpoint)
{
    auto docs = Builder().AddDirectory(fs::NorImageBuilder::rootInode, "docs");

    Builder().Commit();

    EXPECT_EQ(5, Builder().CheckpointBlock());
    EXPECT_EQ(fs::NorBlockType::node, Header(4).allocation->type);
    auto checkpoint = CheckpointIn(5);
    ASSERT_TRUE(checkpoint.has_value());
    EXPECT_EQ(4u, checkpoint->inodeTableRoot / blockSize);
    EXPECT_EQ(Builder().InodeTable().address, checkpoint->inodeTableRoot);
    EXPECT_EQ(1, checkpoint->inodeTableHeight);
    EXPECT_EQ(3u, checkpoint->nextInode);
    EXPECT_EQ(2u, checkpoint->liveNodes);
    EXPECT_TRUE(fs::IsNorBlockInUse(infra::MakeRange(checkpointBytes), 4));
    EXPECT_FALSE(fs::IsNorBlockInUse(infra::MakeRange(checkpointBytes), 3));
    EXPECT_EQ(std::optional<uint32_t>(docs), Builder().EntryOf(fs::NorImageBuilder::rootInode, 0x572bb700));
}

TEST_F(NorImageBuilderTest, commit_fills_as_many_node_blocks_as_the_maps_need)
{
    for (uint32_t index = 0; index != 30; ++index)
    {
        std::array<char, 4> name{ 'f', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
        Builder().AddDirectory(fs::NorImageBuilder::rootInode, infra::BoundedConstString(name.data(), 3));
    }

    Builder().Commit();

    EXPECT_EQ(fs::NorBlockType::node, Header(4).allocation->type);
    EXPECT_EQ(fs::NorBlockType::node, Header(5).allocation->type);
    EXPECT_EQ(6, Builder().CheckpointBlock());
    EXPECT_EQ(2, Builder().InodeTable().height);
    EXPECT_EQ(fs::NorImageBuilder::rootInode, Builder().RecordOf(31).value_or(fs::NorInodeRecord{}).parent);
}
