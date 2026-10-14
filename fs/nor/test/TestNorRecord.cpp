#include "fs/nor/NorRecord.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace
{
    constexpr uint32_t inodeRecordBytes = 6;
    constexpr std::array<uint8_t, inodeRecordBytes> inodeRecord{ 1, 2, 3, 4, 5, 6 };
    constexpr std::array<uint8_t, 3> inlineBytes{ 0xa, 0xb, 0xc };

    class NorRecordTest
        : public testing::Test
    {
    public:
        fs::OpenedRecord Write(std::initializer_list<fs::NorEntry> entries)
        {
            fs::CrcRecord record{ infra::MakeRange(buffer) };
            for (const auto& entry : entries)
                fs::AddNorEntry(record, entry);

            record.Seal();
            return *fs::OpenRecord(infra::MakeRange(buffer));
        }

        fs::OpenedRecord WriteRaw(uint8_t entries, std::initializer_list<uint8_t> bytes)
        {
            fs::CrcRecord record{ infra::MakeRange(buffer) };
            for (uint8_t entry = 1; entry < entries; ++entry)
                record.Add(0);

            auto payload = record.Add(bytes.size());
            std::copy(bytes.begin(), bytes.end(), payload.begin());
            record.Seal();
            return *fs::OpenRecord(infra::MakeRange(buffer));
        }

        std::array<uint8_t, 256> buffer{};
    };
}

TEST_F(NorRecordTest, every_entry_reads_back)
{
    std::initializer_list<fs::NorEntry> entries{
        fs::NorLink{ 17, 400 },
        fs::NorInodePut{ 3, infra::MakeRange(inodeRecord) },
        fs::NorInodeRemove{ 4 },
        fs::NorDirectoryEntryPut{ 1, 0x12345600, 3 },
        fs::NorDirectoryEntryRemove{ 1, 0x12345601 },
        fs::NorBlockSet{ 3, 2, 40, 41 },
        fs::NorSize{ 3, 9000, infra::MakeRange(inlineBytes) }
    };
    fs::NorEntryReader reader{ Write(entries), inodeRecordBytes };

    for (const auto& entry : entries)
        EXPECT_EQ(entry, reader.Next());

    EXPECT_EQ(std::nullopt, reader.Next());
    EXPECT_FALSE(reader.Malformed());
}

TEST_F(NorRecordTest, entries_start_with_their_kind_and_hold_their_fields_in_little_endian)
{
    auto opened = Write({ fs::NorBlockSet{ 0x01020304, 1, 2, 3 } });

    EXPECT_EQ((std::array<uint8_t, 17>{ 6, 0x04, 0x03, 0x02, 0x01, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0 }), opened.payload);
}

TEST_F(NorRecordTest, the_size_of_an_entry_is_its_kind_and_fields)
{
    EXPECT_EQ(9u, fs::NorEntrySize(fs::NorLink{}));
    EXPECT_EQ(5u + inodeRecordBytes, fs::NorEntrySize(fs::NorInodePut{ 3, infra::MakeRange(inodeRecord) }));
    EXPECT_EQ(5u, fs::NorEntrySize(fs::NorInodeRemove{}));
    EXPECT_EQ(13u, fs::NorEntrySize(fs::NorDirectoryEntryPut{}));
    EXPECT_EQ(9u, fs::NorEntrySize(fs::NorDirectoryEntryRemove{}));
    EXPECT_EQ(17u, fs::NorEntrySize(fs::NorBlockSet{}));
    EXPECT_EQ(14u, fs::NorEntrySize(fs::NorSize{ 3, 9000, infra::MakeRange(inlineBytes) }));
}

TEST_F(NorRecordTest, a_size_without_inline_bytes_reads_back)
{
    fs::NorEntryReader reader{ Write({ fs::NorSize{ 3, 4060, {} } }), inodeRecordBytes };

    EXPECT_EQ(fs::NorEntry(fs::NorSize{ 3, 4060, {} }), reader.Next());
}

TEST_F(NorRecordTest, entries_with_different_bytes_differ)
{
    std::array<uint8_t, inodeRecordBytes> otherRecord{ 1, 2, 3, 4, 5, 7 };

    EXPECT_NE((fs::NorInodePut{ 3, infra::MakeRange(inodeRecord) }), (fs::NorInodePut{ 3, infra::MakeRange(otherRecord) }));
    EXPECT_NE((fs::NorInodePut{ 3, infra::MakeRange(inodeRecord) }), (fs::NorInodePut{ 4, infra::MakeRange(inodeRecord) }));
    EXPECT_NE((fs::NorSize{ 3, 1, infra::MakeRange(inlineBytes) }), (fs::NorSize{ 4, 1, infra::MakeRange(inlineBytes) }));
    EXPECT_NE((fs::NorSize{ 3, 1, infra::MakeRange(inlineBytes) }), (fs::NorSize{ 3, 2, infra::MakeRange(inlineBytes) }));
    EXPECT_NE((fs::NorSize{ 3, 1, infra::MakeRange(inlineBytes) }), (fs::NorSize{ 3, 1, {} }));
}

TEST_F(NorRecordTest, a_record_without_entries_reads_nothing)
{
    fs::NorEntryReader reader{ Write({}), inodeRecordBytes };

    EXPECT_EQ(std::nullopt, reader.Next());
    EXPECT_FALSE(reader.Malformed());
}

TEST_F(NorRecordTest, an_unknown_kind_is_malformed)
{
    for (uint8_t kind : { 0, 8, 0xff })
    {
        fs::NorEntryReader reader{ WriteRaw(1, { kind, 0, 0, 0, 0 }), inodeRecordBytes };

        EXPECT_EQ(std::nullopt, reader.Next()) << int(kind);
        EXPECT_TRUE(reader.Malformed()) << int(kind);
    }
}

TEST_F(NorRecordTest, an_entry_cut_short_is_malformed)
{
    for (uint8_t kind = 1; kind != 8; ++kind)
    {
        fs::NorEntryReader reader{ WriteRaw(1, { kind, 0, 0 }), inodeRecordBytes };

        EXPECT_EQ(std::nullopt, reader.Next()) << int(kind);
        EXPECT_TRUE(reader.Malformed()) << int(kind);
    }
}

TEST_F(NorRecordTest, inline_bytes_beyond_the_record_are_malformed)
{
    fs::NorEntryReader reader{ WriteRaw(1, { 7, 3, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0xa }), inodeRecordBytes };

    EXPECT_EQ(std::nullopt, reader.Next());
    EXPECT_TRUE(reader.Malformed());
}

TEST_F(NorRecordTest, more_entries_than_the_record_holds_are_malformed)
{
    fs::NorEntryReader reader{ WriteRaw(2, { 3, 4, 0, 0, 0 }), inodeRecordBytes };

    EXPECT_EQ(fs::NorEntry(fs::NorInodeRemove{ 4 }), reader.Next());
    EXPECT_EQ(std::nullopt, reader.Next());
    EXPECT_TRUE(reader.Malformed());
}

TEST_F(NorRecordTest, bytes_after_the_last_entry_are_malformed)
{
    fs::NorEntryReader reader{ WriteRaw(1, { 3, 4, 0, 0, 0, 0 }), inodeRecordBytes };

    EXPECT_EQ(fs::NorEntry(fs::NorInodeRemove{ 4 }), reader.Next());
    EXPECT_EQ(std::nullopt, reader.Next());
    EXPECT_TRUE(reader.Malformed());
}

TEST_F(NorRecordTest, a_malformed_record_stays_malformed)
{
    fs::NorEntryReader reader{ WriteRaw(2, { 0 }), inodeRecordBytes };

    EXPECT_EQ(std::nullopt, reader.Next());
    EXPECT_EQ(std::nullopt, reader.Next());
    EXPECT_TRUE(reader.Malformed());
}

TEST(NorLargestRecordTest, holds_a_link_and_a_rename_or_eight_block_entries_with_a_size)
{
    static_assert(fs::NorLargestRecordBytes(64, 64) == 228);
    static_assert(fs::NorLargestRecordBytes(64, 255) == 8 + 9 + 9 + 13 + 5 + 20 + 64 + 255);
    static_assert(fs::NorLargestRecordBytes(255, 1) == 8 + 9 + 8 * 17 + 11 + 255);
}
