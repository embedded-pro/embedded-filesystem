#include "fs/nor/NorConfig.hpp"
#include "gtest/gtest.h"

namespace
{
    constexpr fs::NorConfig valid{ .maxBlocks = 4096 };
}

TEST(NorConfigTest, header_rounds_both_parts_to_the_program_unit)
{
    static_assert(fs::NorHeaderSize(1) == 36);
    static_assert(fs::NorHeaderSize(4) == 36);
    static_assert(fs::NorHeaderSize(8) == 40);
    static_assert(fs::NorHeaderSize(16) == 48);
    static_assert(fs::NorHeaderPartBOffset(16) == 16);
}

TEST(NorConfigTest, inode_record_holds_the_fixed_fields_the_inline_area_and_the_name)
{
    static_assert(fs::NorInodeRecordBytes(valid) == 148);
}

TEST(NorConfigTest, node_capacities_follow_from_the_node_size)
{
    static_assert(fs::NorNodeEntryBytes(512) == 496);
    static_assert(fs::NorRadixFanout(512) == 124);
    static_assert(fs::NorKeyedEntries(512, 4) == 62);
    static_assert(fs::NorKeyedEntries(512, fs::NorInodeRecordBytes(valid)) == 3);
}

TEST(NorConfigTest, the_defaults_with_a_number_of_blocks_are_valid)
{
    static_assert(fs::norMinimumCachedNodes == 15);
    static_assert(fs::IsValidNorConfig(valid));
    static_assert(fs::IsValidNorConfig(fs::NorConfig{ .maxBlocks = 256, .programUnit = 16, .pageSize = 16 }));
}
