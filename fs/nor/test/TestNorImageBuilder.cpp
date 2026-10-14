#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <optional>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t firstRecord = headerSize + 36;

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
    fs::NorEntryReader reader{ *record, fs::NorInodeRecordBytes(config) };
    EXPECT_EQ(fs::NorEntry(fs::NorLink{ 3, end }), reader.Next());
    EXPECT_EQ(fs::NorEntry(fs::NorInodeRemove{ 8 }), reader.Next());
    EXPECT_EQ(8, Builder().JournalBlock());
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
