#include "fs/fat/FatPartitionTable.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    using Sector = std::array<uint8_t, 512>;

    void SetEntry(Sector& sector, std::size_t index, uint8_t type, uint32_t first, uint32_t sectors)
    {
        auto offset = 446 + index * 16;
        sector[offset + 4] = type;

        for (std::size_t byte = 0; byte != 4; ++byte)
        {
            sector[offset + 8 + byte] = static_cast<uint8_t>(first >> (8 * byte));
            sector[offset + 12 + byte] = static_cast<uint8_t>(sectors >> (8 * byte));
        }
    }

    Sector Mbr()
    {
        Sector sector{};
        sector[0] = 0xfa;
        sector[510] = 0x55;
        sector[511] = 0xaa;
        return sector;
    }

    std::optional<fs::PartitionEntry> FirstFat(const Sector& sector)
    {
        return fs::FirstFatPartition(infra::MakeRange(sector));
    }
}

class FatPartitionTypeTest
    : public testing::TestWithParam<uint8_t>
{};

TEST_P(FatPartitionTypeTest, finds_fat_partition)
{
    auto sector = Mbr();
    SetEntry(sector, 0, GetParam(), 8192, 516096);

    EXPECT_EQ((fs::PartitionEntry{ GetParam(), 8192, 516096 }), FirstFat(sector));
}

INSTANTIATE_TEST_SUITE_P(EveryFatType, FatPartitionTypeTest, testing::Values(0x01, 0x04, 0x06, 0x0b, 0x0c, 0x0e));

TEST(FatPartitionTableTest, skips_partitions_of_other_types)
{
    auto sector = Mbr();
    SetEntry(sector, 0, 0x83, 2048, 4096);
    SetEntry(sector, 1, 0x0c, 8192, 65536);

    EXPECT_EQ((fs::PartitionEntry{ 0x0c, 8192, 65536 }), FirstFat(sector));
}

TEST(FatPartitionTableTest, skips_empty_fat_partitions)
{
    auto sector = Mbr();
    SetEntry(sector, 0, 0x06, 2048, 0);
    SetEntry(sector, 3, 0x06, 4096, 1024);

    EXPECT_EQ((fs::PartitionEntry{ 0x06, 4096, 1024 }), FirstFat(sector));
}

TEST(FatPartitionTableTest, table_without_fat_partition_has_none)
{
    auto sector = Mbr();
    SetEntry(sector, 0, 0x83, 2048, 4096);

    EXPECT_EQ(std::nullopt, FirstFat(sector));
}

TEST(FatPartitionTableTest, sector_without_signature_has_no_partition)
{
    auto sector = Mbr();
    SetEntry(sector, 0, 0x0c, 8192, 65536);
    sector[510] = 0;

    EXPECT_EQ(std::nullopt, FirstFat(sector));
    EXPECT_FALSE(fs::IsProtectiveMbr(infra::MakeRange(sector)));
}

TEST(FatPartitionTableTest, protective_gpt_is_not_a_fat_partition)
{
    auto sector = Mbr();
    SetEntry(sector, 0, 0xee, 1, 0xffffffff);

    EXPECT_EQ(std::nullopt, FirstFat(sector));
    EXPECT_TRUE(fs::IsProtectiveMbr(infra::MakeRange(sector)));
}

TEST(FatPartitionTableTest, fat_partition_table_is_not_protective)
{
    auto sector = Mbr();
    SetEntry(sector, 0, 0x0c, 8192, 65536);

    EXPECT_FALSE(fs::IsProtectiveMbr(infra::MakeRange(sector)));
}

TEST(FatPartitionTableTest, serializes_one_primary_partition)
{
    Sector sector;
    sector.fill(0xcc);

    fs::SerializeMbr(fs::PartitionEntry{ 0x06, 8031, 516257 }, infra::MakeRange(sector));

    EXPECT_EQ((fs::PartitionEntry{ 0x06, 8031, 516257 }), FirstFat(sector));
    EXPECT_EQ(0x00, sector[0]);
    EXPECT_EQ(0x00, sector[446]);
    EXPECT_EQ(0xfe, sector[447]);
    EXPECT_EQ(0xff, sector[448]);
    EXPECT_EQ(0xff, sector[449]);
    EXPECT_EQ(0xfe, sector[451]);
    EXPECT_EQ(0x00, sector[462]);
    EXPECT_EQ(0x55, sector[510]);
    EXPECT_EQ(0xaa, sector[511]);
}
