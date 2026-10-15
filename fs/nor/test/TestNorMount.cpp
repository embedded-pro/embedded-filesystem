#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorMount.hpp"
#include "fs/nor/NorSuperblock.hpp"
#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t firstRecord = headerSize + 36;
    constexpr uint32_t rootNode = 2 * blockSize + 256;
    constexpr std::size_t largestRecord = fs::NorLargestRecordBytes(config.inlineBytes, config.maxNameBytes);
    constexpr fs::NorConfig wideUnitConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    using State = fs::NorBlockState;

    fs::NorConfig With(const std::function<void(fs::NorConfig&)>& change)
    {
        auto changed = config;
        change(changed);
        return changed;
    }

    class NorMountTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        NorMountTest()
        {
            Reformat(32, blockSize, config);
        }

        void Reformat(uint32_t sectors, uint32_t sectorSize, const fs::NorConfig& formatConfig)
        {
            flash.sectors.assign(sectors, std::vector<uint8_t>(sectorSize, 0xff));
            volumeConfig = formatConfig;
            builder.reset();
            std::array<uint8_t, 1024> formatBuffer{};
            fs::NorFormatter formatter{ flash, formatConfig, infra::MakeRange(formatBuffer) };
            formatter.Format([](fs::Result) {});
            ExecuteAllActions();
        }

        fs::NorImageBuilder& Builder()
        {
            if (!builder.has_value())
                builder.emplace(image, volumeConfig);

            return *builder;
        }

        std::optional<fs::Result> Mount()
        {
            return Mount(volumeConfig);
        }

        std::optional<fs::Result> Mount(const fs::NorConfig& mountConfig)
        {
            std::optional<fs::Result> result;
            fs::NorMount mount{ flash, mountConfig, volume, infra::MakeRange(buffer), [&result](fs::Result mounted)
                {
                    result = mounted;
                } };
            mount.Start();
            EXPECT_EQ(std::nullopt, result);
            ExecuteAllActions();
            return result;
        }

        State StateOf(uint32_t block) const
        {
            return volume.blocks.State(block);
        }

        void WriteSuperblock(uint32_t block, const fs::NorSuperblock& superblock)
        {
            std::array<uint8_t, 28> bytes{};
            fs::SerializeNorSuperblock(superblock, infra::MakeRange(bytes));
            Builder().EraseBlock(block);
            Builder().Program(block * blockSize, infra::MakeRange(bytes));
        }

        void WriteNewCheckpoint(const std::function<void(fs::NorCheckpoint&)>& change, std::initializer_list<uint32_t> used = { 0, 1, 2, 5 })
        {
            auto checkpoint = Builder().Checkpoint();
            checkpoint.recordsBlock = 5;
            change(checkpoint);
            Builder().Allocate(5, fs::NorBlockType::checkpoint);
            Builder().WriteCheckpoint(5, checkpoint, used);
        }

        hal::FlashStub flash{ 32, blockSize };
        fs::NorFlashStubImage image{ flash };
        fs::NorConfig volumeConfig{ config };
        std::optional<fs::NorImageBuilder> builder;
        std::array<uint8_t, fs::NorBlockStateBytes(48)> stateStorage{};
        std::array<uint8_t, 48> countStorage{};
        std::array<uint8_t, fs::NorNodeBlockBytes(48)> nodeBlockStorage{};
        fs::NorVolumeState volume{ infra::MakeRange(stateStorage), infra::MakeRange(countStorage), infra::MakeRange(nodeBlockStorage) };
        std::array<uint8_t, largestRecord> buffer{};
    };
}

TEST_F(NorMountTest, mounts_a_formatted_volume)
{
    EXPECT_EQ(fs::Result::success, Mount());

    ASSERT_TRUE(volume.geometry.has_value());
    EXPECT_EQ(32, volume.geometry->blocks);
    EXPECT_EQ((fs::NorCheckpoint{ rootNode, 1, 2, 3, firstRecord, 1, 32 }), volume.checkpoint);
    EXPECT_EQ((fs::NorMapRoot{ rootNode, 1 }), volume.inodeTable);
    EXPECT_EQ(3, volume.checkpointBlock);
    EXPECT_EQ((fs::NorJournal{ 3, firstRecord, 1 }), volume.journal);
    EXPECT_EQ(3, volume.nextSequence);
}

TEST_F(NorMountTest, takes_the_next_inode_and_the_live_nodes_from_the_checkpoint_and_starts_the_journal_and_the_nodes_in_new_blocks)
{
    volume.nodeBlock = 9;

    Mount();

    EXPECT_EQ(2, volume.nextInode);
    EXPECT_EQ(1, volume.liveNodes);
    EXPECT_TRUE(volume.startNewJournalBlock);
    EXPECT_EQ(std::nullopt, volume.nodeBlock);
    EXPECT_THAT(volume.epoch, testing::ElementsAre(3));
}

TEST_F(NorMountTest, marks_the_superblocks_and_the_blocks_of_the_checkpoint_in_use_and_every_other_block_free_and_erased)
{
    Mount();

    for (uint32_t block = 0; block != 4; ++block)
        EXPECT_EQ(State::inUse, StateOf(block)) << block;
    for (uint32_t block = 4; block != 32; ++block)
        EXPECT_EQ(State::freeErased, StateOf(block)) << block;
}

TEST_F(NorMountTest, keeps_the_erase_counts_relative_to_the_lowest)
{
    Builder().SetEraseCount(5, 7);
    Builder().SetEraseCount(6, 30);

    Mount();

    EXPECT_EQ(1, volume.eraseCounts.Base());
    EXPECT_EQ(7, volume.eraseCounts.Count(5));
    EXPECT_EQ(30, volume.eraseCounts.Count(6));
    EXPECT_EQ(1, volume.eraseCounts.Count(31));
}

TEST_F(NorMountTest, estimates_an_unknown_erase_count_with_the_average_of_the_others)
{
    Builder().SetEraseCount(5, 31);
    Builder().EraseBlock(7);

    Mount();

    EXPECT_EQ(2, volume.eraseCounts.Count(7));
    EXPECT_EQ(State::freeNotErased, StateOf(7));
}

TEST_F(NorMountTest, a_block_with_a_part_b_that_the_checkpoint_does_not_use_is_free_and_not_erased)
{
    Builder().Allocate(8, fs::NorBlockType::data, { 5, 0 });

    Mount();

    EXPECT_EQ(State::freeNotErased, StateOf(8));
}

TEST_F(NorMountTest, continues_the_sequence_after_the_highest_one_found)
{
    Builder().Allocate(8, fs::NorBlockType::data, { 5, 0 });
    Builder().Allocate(9, fs::NorBlockType::node);

    Mount();

    EXPECT_EQ(5, volume.nextSequence);
}

TEST_F(NorMountTest, reads_the_second_superblock_when_the_first_is_not_valid)
{
    Builder().Corrupt(1);

    EXPECT_EQ(fs::Result::success, Mount());
}

TEST_F(NorMountTest, a_device_without_a_valid_superblock_is_corrupted)
{
    Builder().Corrupt(1);
    Builder().Corrupt(blockSize + 1);

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, a_superblock_that_names_another_number_of_blocks_is_corrupted)
{
    WriteSuperblock(0, { 1, blockSize, 31, 1, 512, 64, 64 });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, a_superblock_that_names_another_sector_size_is_corrupted)
{
    WriteSuperblock(0, { 1, 2048, 32, 1, 512, 64, 64 });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, other_format_parameters_are_unsupported)
{
    auto unsupported = {
        With([](fs::NorConfig& changed)
            {
                changed.programUnit = 2;
            }),
        With([](fs::NorConfig& changed)
            {
                changed.nodeSize = 1024;
            }),
        With([](fs::NorConfig& changed)
            {
                changed.inlineBytes = 48;
            }),
        With([](fs::NorConfig& changed)
            {
                changed.maxNameBytes = 32;
            }),
    };

    for (const auto& changed : unsupported)
        EXPECT_EQ(fs::Result::unsupported, Mount(changed));
}

TEST_F(NorMountTest, other_sectors_per_block_are_unsupported)
{
    EXPECT_EQ(fs::Result::unsupported, Mount(With([](fs::NorConfig& changed)
                                           {
                                               changed.sectorsPerBlock = 2;
                                           })));
}

TEST_F(NorMountTest, a_device_that_does_not_fit_the_configuration_is_unsupported)
{
    EXPECT_EQ(fs::Result::unsupported, Mount(With([](fs::NorConfig& changed)
                                           {
                                               changed.maxBlocks = 16;
                                           })));
}

TEST_F(NorMountTest, a_device_with_sectors_of_several_sizes_is_unsupported)
{
    flash.sectors[5].resize(2048);

    EXPECT_EQ(fs::Result::unsupported, Mount());
}

TEST_F(NorMountTest, uses_the_checkpoint_with_the_highest_sequence)
{
    Builder().WriteCheckpoint(5);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(5, volume.checkpointBlock);
    EXPECT_EQ((fs::NorJournal{ 5, firstRecord, 1 }), volume.journal);
    EXPECT_EQ(State::inUse, StateOf(5));
    EXPECT_EQ(State::freeNotErased, StateOf(3));
}

TEST_F(NorMountTest, skips_a_checkpoint_whose_crc_is_not_valid)
{
    Builder().WriteCheckpoint(5);
    Builder().Corrupt(5 * blockSize + headerSize + 1);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(3, volume.checkpointBlock);
    EXPECT_EQ(State::freeNotErased, StateOf(5));
}

TEST_F(NorMountTest, skips_two_checkpoints_whose_crc_is_not_valid)
{
    Builder().WriteCheckpoint(5);
    Builder().WriteCheckpoint(6);
    Builder().Corrupt(5 * blockSize + headerSize + 1);
    Builder().Corrupt(6 * blockSize + headerSize + 1);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(3, volume.checkpointBlock);
}

TEST_F(NorMountTest, skips_a_checkpoint_whose_bitmap_would_not_fit_its_block)
{
    Builder().Allocate(5, fs::NorBlockType::checkpoint);
    Builder().Program(5 * blockSize + headerSize + 27, infra::MakeRange(std::array<uint8_t, 1>{ 0x0f }));

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(3, volume.checkpointBlock);
}

TEST_F(NorMountTest, a_device_without_a_valid_checkpoint_is_corrupted)
{
    Builder().Corrupt(3 * blockSize + headerSize + 1);

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, marks_the_blocks_that_the_checkpoint_uses_in_use)
{
    Builder().Allocate(9, fs::NorBlockType::data, { 4, 0 });
    WriteNewCheckpoint([](fs::NorCheckpoint&) {}, { 0, 1, 2, 5, 9 });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::inUse, StateOf(9));
}

TEST_F(NorMountTest, marks_the_node_block_of_a_formatted_volume_as_node_block)
{
    Mount();

    EXPECT_EQ(1, volume.nodeBlocks.Count());
    EXPECT_TRUE(volume.nodeBlocks.Contains(2));
}

TEST_F(NorMountTest, marks_the_node_blocks_that_the_checkpoint_uses_and_no_other_block)
{
    Builder().Allocate(8, fs::NorBlockType::node);
    Builder().Allocate(9, fs::NorBlockType::node);
    Builder().Allocate(10, fs::NorBlockType::data, { 4, 0 });
    WriteNewCheckpoint([](fs::NorCheckpoint&) {}, { 0, 1, 2, 5, 9, 10 });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(2, volume.nodeBlocks.Count());
    EXPECT_TRUE(volume.nodeBlocks.Contains(2));
    EXPECT_FALSE(volume.nodeBlocks.Contains(8));
    EXPECT_TRUE(volume.nodeBlocks.Contains(9));
    EXPECT_FALSE(volume.nodeBlocks.Contains(10));
}

TEST_F(NorMountTest, forgets_the_node_blocks_of_an_earlier_mount)
{
    volume.nodeBlocks.Reset(32);
    volume.nodeBlocks.Add(7);

    Mount();

    EXPECT_FALSE(volume.nodeBlocks.Contains(7));
}

TEST_F(NorMountTest, never_skips_a_valid_checkpoint_that_is_not_consistent)
{
    using Change = void (*)(fs::NorCheckpoint&);
    const std::array<Change, 13> changes{
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.blocks = 30;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableHeight = 0;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableHeight = 4;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableRoot += 4;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableRoot = blockSize + 256;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.nextInode = 1;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableRoot = 40 * blockSize + 256;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableRoot = 2 * blockSize;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableRoot = 2 * blockSize + 256 + 7 * 512;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsBlock = 1;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsBlock = 40;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsOffset = headerSize;
        },
        [](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsOffset = blockSize + 1;
        },
    };

    for (auto change : changes)
    {
        Reformat(32, blockSize, config);
        WriteNewCheckpoint(change);

        EXPECT_EQ(fs::Result::corrupted, Mount());
    }
}

TEST_F(NorMountTest, a_checkpoint_that_does_not_mark_its_own_block_is_corrupted)
{
    WriteNewCheckpoint([](fs::NorCheckpoint&) {}, { 0, 1, 2 });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, a_block_that_the_checkpoint_marks_is_in_use_even_when_erased_because_a_record_may_have_released_it)
{
    WriteNewCheckpoint([](fs::NorCheckpoint&) {}, { 0, 1, 2, 5, 9 });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::inUse, StateOf(9));
}

TEST_F(NorMountTest, a_block_that_the_checkpoint_marks_without_part_a_is_in_use_with_an_estimated_erase_count)
{
    Builder().SetEraseCount(5, 31);
    Builder().EraseBlock(9);
    WriteNewCheckpoint([](fs::NorCheckpoint&) {}, { 0, 1, 2, 5, 9 });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::inUse, StateOf(9));
    EXPECT_EQ(2, volume.eraseCounts.Count(9));
}

TEST_F(NorMountTest, a_checkpoint_whose_root_is_in_a_block_that_it_does_not_use_is_corrupted)
{
    WriteNewCheckpoint([](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.inodeTableRoot = 9 * blockSize + 256;
        });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, records_that_start_outside_a_checkpoint_or_journal_block_are_corrupted)
{
    WriteNewCheckpoint([](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsBlock = 2;
            checkpoint.recordsOffset = headerSize;
        });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, records_that_start_in_a_block_that_the_checkpoint_does_not_use_are_corrupted)
{
    Builder().Allocate(8, fs::NorBlockType::journal);
    WriteNewCheckpoint([](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsBlock = 8;
            checkpoint.recordsOffset = headerSize;
        });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, records_may_start_in_a_journal_block_that_the_checkpoint_uses)
{
    Builder().Allocate(8, fs::NorBlockType::journal);
    WriteNewCheckpoint([](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsBlock = 8;
            checkpoint.recordsOffset = headerSize;
        },
        { 0, 1, 2, 5, 8 });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ((fs::NorJournal{ 8, headerSize, 1 }), volume.journal);
}

TEST_F(NorMountTest, records_that_start_at_the_end_of_their_block_are_empty)
{
    WriteNewCheckpoint([](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsOffset = blockSize;
        });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ((fs::NorJournal{ 5, blockSize, 1 }), volume.journal);
}

TEST_F(NorMountTest, finds_the_end_of_the_records_after_the_checkpoint)
{
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    Builder().AppendRecord({ fs::NorDirectoryEntryRemove{ 1, 0x100 } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ((fs::NorJournal{ 3, Builder().JournalEnd(), 1 }), volume.journal);
}

TEST_F(NorMountTest, the_records_end_at_a_record_whose_crc_is_not_valid)
{
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    auto end = Builder().JournalEnd();
    Builder().AppendRecord({ fs::NorInodeRemove{ 8 } });
    Builder().Corrupt(3 * blockSize + end + 5);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ((fs::NorJournal{ 3, end, 1 }), volume.journal);
}

TEST_F(NorMountTest, the_records_end_at_a_length_that_no_record_has)
{
    Builder().Program(3 * blockSize + firstRecord, infra::MakeRange(std::array<uint8_t, 2>{ 7, 0 }));

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(firstRecord, volume.journal.end);
}

TEST_F(NorMountTest, the_records_end_at_a_record_longer_than_the_journal_buffer)
{
    Builder().Program(3 * blockSize + firstRecord, infra::MakeRange(std::array<uint8_t, 2>{ 0, 2 }));

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(firstRecord, volume.journal.end);
}

TEST_F(NorMountTest, follows_the_journal_into_linked_journal_blocks)
{
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    Builder().StartJournalBlock(8);
    Builder().AppendRecord({ fs::NorInodeRemove{ 8 } });
    Builder().StartJournalBlock(4);
    Builder().AppendRecord({ fs::NorInodeRemove{ 9 } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ((fs::NorJournal{ 4, Builder().JournalEnd(), 3 }), volume.journal);
    EXPECT_THAT(volume.epoch, testing::ElementsAre(3, 8, 4));
    EXPECT_EQ(State::inUse, StateOf(8));
    EXPECT_EQ(State::inUse, StateOf(4));
}

TEST_F(NorMountTest, an_epoch_of_16_blocks_mounts)
{
    for (uint32_t block = 8; block != 23; ++block)
    {
        Builder().StartJournalBlock(block);
        Builder().AppendRecord({});
    }

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(16, volume.journal.blocks);
}

TEST_F(NorMountTest, an_epoch_of_more_than_16_blocks_is_corrupted)
{
    for (uint32_t block = 8; block != 24; ++block)
    {
        Builder().StartJournalBlock(block);
        Builder().AppendRecord({});
    }

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, skips_a_journal_block_whose_link_names_another_end)
{
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    auto end = Builder().JournalEnd();
    Builder().Allocate(8, fs::NorBlockType::journal);
    Builder().AppendRecord(8, headerSize, { fs::NorLink{ 3, end + 1 }, fs::NorInodeRemove{ 8 } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ((fs::NorJournal{ 3, end, 1 }), volume.journal);
    EXPECT_EQ(State::freeNotErased, StateOf(8));
}

TEST_F(NorMountTest, skips_a_journal_block_whose_link_names_another_block)
{
    Builder().Allocate(8, fs::NorBlockType::journal);
    Builder().AppendRecord(8, headerSize, { fs::NorLink{ 4, firstRecord } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::freeNotErased, StateOf(8));
}

TEST_F(NorMountTest, skips_a_journal_block_whose_first_entry_is_not_a_link)
{
    Builder().Allocate(8, fs::NorBlockType::journal);
    Builder().AppendRecord(8, headerSize, { fs::NorInodeRemove{ 8 } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::freeNotErased, StateOf(8));
}

TEST_F(NorMountTest, skips_a_journal_block_without_a_valid_first_record)
{
    Builder().Allocate(8, fs::NorBlockType::journal);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::freeNotErased, StateOf(8));
}

TEST_F(NorMountTest, follows_the_linked_journal_block_with_the_lowest_sequence)
{
    Builder().Allocate(8, fs::NorBlockType::journal);
    Builder().Allocate(9, fs::NorBlockType::journal);
    Builder().AppendRecord(9, headerSize, { fs::NorLink{ 3, firstRecord } });
    Builder().AppendRecord(8, headerSize, { fs::NorLink{ 3, firstRecord } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::inUse, StateOf(8));
    EXPECT_EQ(State::freeNotErased, StateOf(9));
}

TEST_F(NorMountTest, skips_a_journal_block_older_than_the_block_it_would_follow)
{
    Builder().Allocate(8, fs::NorBlockType::journal);
    Builder().WriteCheckpoint(5);
    Builder().AppendRecord(8, headerSize, { fs::NorLink{ 5, firstRecord } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::freeNotErased, StateOf(8));
}

TEST_F(NorMountTest, the_blocks_of_an_earlier_epoch_are_free_and_not_erased)
{
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    Builder().StartJournalBlock(8);
    Builder().AppendRecord({ fs::NorInodeRemove{ 8 } });
    Builder().WriteCheckpoint(5);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::freeNotErased, StateOf(3));
    EXPECT_EQ(State::freeNotErased, StateOf(8));
    EXPECT_EQ((fs::NorJournal{ 5, firstRecord, 1 }), volume.journal);
}

TEST_F(NorMountTest, mounts_again_with_the_state_of_the_second_mount)
{
    Mount();
    Builder().WriteCheckpoint(5);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(5, volume.checkpointBlock);
    EXPECT_EQ(State::freeNotErased, StateOf(3));
}

TEST_F(NorMountTest, a_device_without_any_checkpoint_block_is_corrupted)
{
    for (uint32_t block = 2; block != 32; ++block)
        Builder().EraseBlock(block);

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, falls_back_to_the_valid_checkpoint_with_the_next_lower_sequence)
{
    Builder().WriteCheckpoint(6);
    Builder().WriteCheckpoint(4);
    Builder().StartJournalBlock(8);
    Builder().AppendRecord({ fs::NorInodeRemove{ 7 } });
    auto end = Builder().JournalEnd();
    Builder().WriteCheckpoint(5);
    Builder().Corrupt(5 * blockSize + headerSize + 1);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(4, volume.checkpointBlock);
    EXPECT_EQ((fs::NorJournal{ 8, end, 2 }), volume.journal);
}

TEST_F(NorMountTest, mounts_a_volume_whose_blocks_are_not_a_multiple_of_eight)
{
    Reformat(30, blockSize, config);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(30, volume.geometry->blocks);
    EXPECT_EQ(State::freeErased, StateOf(29));
}

TEST_F(NorMountTest, mounts_a_volume_with_a_program_unit_of_16_bytes)
{
    Reformat(48, 2048, wideUnitConfig);

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ((fs::NorJournal{ 3, 48 + 48, 1 }), volume.journal);
}

TEST_F(NorMountTest, records_that_do_not_start_at_a_program_unit_are_corrupted)
{
    Reformat(48, 2048, wideUnitConfig);
    WriteNewCheckpoint([](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsOffset = 48 + 48 + 8;
        });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, records_that_start_in_a_block_without_a_valid_part_b_are_corrupted)
{
    Builder().Program(8 * blockSize + 12, infra::MakeRange(std::array<uint8_t, 2>{ 2, 0 }));
    WriteNewCheckpoint([](fs::NorCheckpoint& checkpoint)
        {
            checkpoint.recordsBlock = 8;
            checkpoint.recordsOffset = headerSize;
        },
        { 0, 1, 2, 5, 8 });

    EXPECT_EQ(fs::Result::corrupted, Mount());
}

TEST_F(NorMountTest, skips_a_journal_block_whose_first_record_holds_no_entry)
{
    Builder().Allocate(8, fs::NorBlockType::journal);
    Builder().AppendRecord(8, headerSize, {});

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::freeNotErased, StateOf(8));
}

TEST_F(NorMountTest, a_superblock_with_more_blocks_than_the_configuration_allows_is_unsupported)
{
    WriteSuperblock(0, { 1, blockSize, 33, 1, 512, 64, 64 });

    EXPECT_EQ(fs::Result::unsupported, Mount());
}

TEST_F(NorMountTest, a_device_without_sectors_is_unsupported)
{
    flash.sectors.clear();

    EXPECT_EQ(fs::Result::unsupported, Mount());
}

TEST_F(NorMountTest, follows_the_linked_journal_block_with_the_lowest_sequence_when_a_higher_one_comes_first)
{
    Builder().Allocate(9, fs::NorBlockType::journal);
    Builder().Allocate(8, fs::NorBlockType::journal);
    Builder().AppendRecord(9, headerSize, { fs::NorLink{ 3, firstRecord } });
    Builder().AppendRecord(8, headerSize, { fs::NorLink{ 3, firstRecord } });

    EXPECT_EQ(fs::Result::success, Mount());
    EXPECT_EQ(State::inUse, StateOf(9));
    EXPECT_EQ(State::freeNotErased, StateOf(8));
}
