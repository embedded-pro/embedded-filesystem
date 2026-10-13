#include "fs/fat/FatTable.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    using Sector = std::array<uint8_t, 512>;

    uint32_t Read(fs::FatType type, uint32_t cluster, const Sector& sector, const Sector& next)
    {
        return fs::ReadEntry(type, cluster, infra::MakeRange(sector), infra::MakeRange(next));
    }

    void Write(fs::FatType type, uint32_t cluster, uint32_t value, Sector& sector, Sector& next)
    {
        fs::WriteEntry(type, cluster, value, infra::MakeRange(sector), infra::MakeRange(next));
    }
}

TEST(FatTableTest, locates_fat12_entries)
{
    EXPECT_EQ((fs::FatEntryLocation{ 0, 0, false }), fs::LocateEntry(fs::FatType::fat12, 0, 512));
    EXPECT_EQ((fs::FatEntryLocation{ 0, 1, false }), fs::LocateEntry(fs::FatType::fat12, 1, 512));
    EXPECT_EQ((fs::FatEntryLocation{ 0, 3, false }), fs::LocateEntry(fs::FatType::fat12, 2, 512));
    EXPECT_EQ((fs::FatEntryLocation{ 0, 511, true }), fs::LocateEntry(fs::FatType::fat12, 341, 512));
    EXPECT_EQ((fs::FatEntryLocation{ 1, 1, false }), fs::LocateEntry(fs::FatType::fat12, 342, 512));
}

TEST(FatTableTest, locates_fat16_and_fat32_entries)
{
    EXPECT_EQ((fs::FatEntryLocation{ 1, 2, false }), fs::LocateEntry(fs::FatType::fat16, 257, 512));
    EXPECT_EQ((fs::FatEntryLocation{ 0, 510, false }), fs::LocateEntry(fs::FatType::fat16, 255, 512));
    EXPECT_EQ((fs::FatEntryLocation{ 2, 4, false }), fs::LocateEntry(fs::FatType::fat32, 257, 512));
}

TEST(FatTableTest, fat12_even_and_odd_entries_share_a_byte)
{
    Sector sector{};
    Sector next{};

    Write(fs::FatType::fat12, 2, 0x123, sector, next);
    Write(fs::FatType::fat12, 3, 0x456, sector, next);

    EXPECT_EQ(0x23, sector[3]);
    EXPECT_EQ(0x61, sector[4]);
    EXPECT_EQ(0x45, sector[5]);
    EXPECT_EQ(0x123u, Read(fs::FatType::fat12, 2, sector, next));
    EXPECT_EQ(0x456u, Read(fs::FatType::fat12, 3, sector, next));
}

TEST(FatTableTest, fat12_write_keeps_the_neighbouring_entry)
{
    Sector sector{};
    Sector next{};

    Write(fs::FatType::fat12, 3, 0x456, sector, next);
    Write(fs::FatType::fat12, 2, 0xfff, sector, next);
    Write(fs::FatType::fat12, 2, 0x000, sector, next);

    EXPECT_EQ(0x000u, Read(fs::FatType::fat12, 2, sector, next));
    EXPECT_EQ(0x456u, Read(fs::FatType::fat12, 3, sector, next));
}

TEST(FatTableTest, fat12_entry_straddling_two_sectors)
{
    Sector sector{};
    Sector next{};
    sector[510] = 0x77;
    next[1] = 0x88;

    Write(fs::FatType::fat12, 341, 0xabc, sector, next);

    EXPECT_EQ(0xc0, sector[511]);
    EXPECT_EQ(0xab, next[0]);
    EXPECT_EQ(0x77, sector[510]);
    EXPECT_EQ(0x88, next[1]);
    EXPECT_EQ(0xabcu, Read(fs::FatType::fat12, 341, sector, next));
}

TEST(FatTableTest, fat12_entry_in_the_next_sector)
{
    Sector sector{};
    Sector next{};

    Write(fs::FatType::fat12, 342, 0x321, sector, next);

    EXPECT_EQ(0x21, sector[1]);
    EXPECT_EQ(0x321u, fs::ReadEntry(fs::FatType::fat12, 342, infra::MakeRange(sector), infra::ConstByteRange()));
}

TEST(FatTableTest, decodes_fat12_entries_from_their_byte_pair)
{
    EXPECT_EQ(0xabcu, fs::DecodeFat12(341, 0xabc0));
    EXPECT_EQ(0xbc0u, fs::DecodeFat12(340, 0xabc0));
}

TEST(FatTableTest, fat16_entry)
{
    Sector sector{};
    Sector next{};

    Write(fs::FatType::fat16, 257, 0xbeef, sector, next);

    EXPECT_EQ(0xef, sector[2]);
    EXPECT_EQ(0xbe, sector[3]);
    EXPECT_EQ(0xbeefu, Read(fs::FatType::fat16, 257, sector, next));
}

TEST(FatTableTest, fat32_entry_keeps_its_upper_four_bits)
{
    Sector sector{};
    Sector next{};
    sector[11] = 0xf0;

    Write(fs::FatType::fat32, 2, 0xfedcba98, sector, next);

    EXPECT_EQ(0x98, sector[8]);
    EXPECT_EQ(0xfe, sector[11]);
    EXPECT_EQ(0x0edcba98u, Read(fs::FatType::fat32, 2, sector, next));
}

TEST(FatTableTest, only_fat12_entries_straddle)
{
    EXPECT_FALSE(fs::LocateEntry(fs::FatType::fat16, 255, 512).straddles);
    EXPECT_FALSE(fs::LocateEntry(fs::FatType::fat32, 127, 512).straddles);
    EXPECT_FALSE(fs::LocateEntry(fs::FatType::fat12, 340, 512).straddles);
}

class FatEntryValuesTest
    : public testing::TestWithParam<std::pair<fs::FatType, uint32_t>>
{};

TEST_P(FatEntryValuesTest, end_of_chain_and_bad_cluster_values)
{
    auto [type, maximum] = GetParam();

    EXPECT_EQ(maximum, fs::EndOfChain(type));
    EXPECT_TRUE(fs::IsEndOfChain(type, maximum));
    EXPECT_TRUE(fs::IsEndOfChain(type, maximum - 7));
    EXPECT_FALSE(fs::IsEndOfChain(type, maximum - 8));
    EXPECT_TRUE(fs::IsBadCluster(type, maximum - 8));
    EXPECT_FALSE(fs::IsBadCluster(type, maximum - 9));
    EXPECT_FALSE(fs::IsBadCluster(type, maximum));
    EXPECT_FALSE(fs::IsEndOfChain(type, 2));
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatEntryValuesTest,
    testing::Values(std::make_pair(fs::FatType::fat12, 0xfffu), std::make_pair(fs::FatType::fat16, 0xffffu), std::make_pair(fs::FatType::fat32, 0x0fffffffu)));

TEST(FatTableTest, clean_shutdown_bit_of_fat16)
{
    EXPECT_EQ(true, fs::CleanShutdown(fs::FatType::fat16, 0xffff));
    EXPECT_EQ(false, fs::CleanShutdown(fs::FatType::fat16, 0x7fff));
    EXPECT_EQ(0x7fffu, fs::WithCleanShutdown(fs::FatType::fat16, 0xffff, false));
    EXPECT_EQ(0xffffu, fs::WithCleanShutdown(fs::FatType::fat16, 0x7fff, true));
}

TEST(FatTableTest, clean_shutdown_bit_of_fat32)
{
    EXPECT_EQ(true, fs::CleanShutdown(fs::FatType::fat32, 0x0fffffff));
    EXPECT_EQ(false, fs::CleanShutdown(fs::FatType::fat32, 0x07ffffff));
    EXPECT_EQ(0x07ffffffu, fs::WithCleanShutdown(fs::FatType::fat32, 0x0fffffff, false));
    EXPECT_EQ(0x0fffffffu, fs::WithCleanShutdown(fs::FatType::fat32, 0x07ffffff, true));
}

TEST(FatTableTest, fat12_has_no_clean_shutdown_bit)
{
    EXPECT_EQ(std::nullopt, fs::CleanShutdown(fs::FatType::fat12, 0xfff));
}
