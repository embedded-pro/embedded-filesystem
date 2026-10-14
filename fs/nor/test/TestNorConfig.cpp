#include "fs/nor/NorConfig.hpp"
#include "gtest/gtest.h"

namespace
{
    constexpr fs::NorConfig valid{ .maxBlocks = 4096 };

    template<class Change>
    constexpr bool ValidAfter(Change change)
    {
        auto config = valid;
        change(config);
        return fs::IsValidNorConfig(config);
    }
}

TEST(NorConfigTest, header_rounds_both_parts_to_the_program_unit)
{
    static_assert(fs::NorHeaderSize(1) == 36u);
    static_assert(fs::NorHeaderSize(4) == 36u);
    static_assert(fs::NorHeaderSize(8) == 40u);
    static_assert(fs::NorHeaderSize(16) == 48u);
    static_assert(fs::NorHeaderPartBOffset(16) == 16u);
}

TEST(NorConfigTest, inode_record_holds_the_fixed_fields_the_inline_area_and_the_name)
{
    static_assert(fs::NorInodeRecordBytes(valid) == 148u);
}

TEST(NorConfigTest, node_capacities_follow_from_the_node_size)
{
    static_assert(fs::NorNodeEntryBytes(512) == 496u);
    static_assert(fs::NorRadixFanout(512) == 124u);
    static_assert(fs::NorKeyedEntries(512, 4) == 62u);
    static_assert(fs::NorKeyedEntries(512, fs::NorInodeRecordBytes(valid)) == 3u);
}

TEST(NorConfigTest, the_defaults_with_a_number_of_blocks_are_valid)
{
    static_assert(fs::IsValidNorConfig(valid));
    static_assert(fs::IsValidNorConfig(fs::NorConfig{ .maxBlocks = 256, .programUnit = 16, .pageSize = 16 }));
}

TEST(NorConfigTest, a_volume_needs_blocks_and_sectors)
{
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.maxBlocks = 3;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.sectorsPerBlock = 0;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.sectorsPerBlock = 0x10000;
        }));
}

TEST(NorConfigTest, program_unit_and_page_are_powers_of_two_with_the_page_at_least_the_program_unit)
{
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.programUnit = 0;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.programUnit = 3;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.pageSize = 96;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.programUnit = 512;
        }));
}

TEST(NorConfigTest, a_node_is_a_whole_number_of_pages_and_holds_two_inode_records)
{
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 640;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 256;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 0x10000;
        }));
    static_assert(ValidAfter([](fs::NorConfig& config)
        {
            config.nodeSize = 1024;
        }));
}

TEST(NorConfigTest, the_cache_holds_at_least_the_largest_operation_and_a_checkpoint)
{
    static_assert(fs::norMinimumCachedNodes == 15);
    static_assert(ValidAfter([](fs::NorConfig& config)
        {
            config.cachedNodes = 15;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.cachedNodes = 14;
        }));
}

TEST(NorConfigTest, the_inline_area_holds_at_least_the_block_header)
{
    static_assert(ValidAfter([](fs::NorConfig& config)
        {
            config.inlineBytes = 36;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.inlineBytes = 35;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.programUnit = 16;
            config.pageSize = 16;
            config.inlineBytes = 47;
        }));
}

TEST(NorConfigTest, names_are_1_to_255_bytes_and_shorter_than_a_path)
{
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.maxNameBytes = 0;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.maxNameBytes = 256;
            config.nodeSize = 2048;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.maxPathBytes = 64;
        }));
}

TEST(NorConfigTest, an_epoch_spans_at_least_two_blocks)
{
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.journalBlocks = 1;
        }));
}

TEST(NorConfigTest, wear_leveling_needs_a_threshold_that_relative_counts_can_hold_and_an_interval)
{
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.staticWearLevelingThreshold = 0;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.staticWearLevelingThreshold = 129;
        }));
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.staticWearLevelingInterval = 0;
        }));
}

TEST(NorConfigTest, a_volume_has_a_slot_for_operations_on_paths)
{
    static_assert(!ValidAfter([](fs::NorConfig& config)
        {
            config.pathOperations = 0;
        }));
}
