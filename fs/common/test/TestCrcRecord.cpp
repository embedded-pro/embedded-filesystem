#include "fs/common/CrcRecord.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
    std::array<uint8_t, 16> TwoEntries()
    {
        std::array<uint8_t, 16> buffer{};
        fs::CrcRecord record{ infra::MakeRange(buffer) };
        auto first = record.Add(2);
        first[0] = 0x11;
        first[1] = 0x22;
        record.Add(1)[0] = 0x33;
        record.Seal();
        return buffer;
    }
}

TEST(CrcRecordTest, seals_the_length_the_entries_and_the_crc)
{
    auto buffer = TwoEntries();

    EXPECT_EQ((std::array<uint8_t, 16>{ 0x0b, 0x00, 0x02, 0x00, 0x11, 0x22, 0x33, 0x95, 0x36, 0x2d, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00 }), buffer);
}

TEST(CrcRecordTest, seal_returns_the_record)
{
    std::array<uint8_t, 16> buffer{};
    fs::CrcRecord record{ infra::MakeRange(buffer) };
    record.Add(3);

    auto sealed = record.Seal();

    EXPECT_EQ(buffer.data(), sealed.begin());
    EXPECT_EQ(11u, sealed.size());
}

TEST(CrcRecordTest, a_record_without_entries_holds_its_header_and_crc)
{
    std::array<uint8_t, 8> buffer{};
    fs::CrcRecord record{ infra::MakeRange(buffer) };

    EXPECT_EQ(fs::CrcRecord::overhead, record.Seal().size());
    EXPECT_EQ(0u, fs::OpenRecord(infra::MakeRange(buffer))->entries);
}

TEST(CrcRecordTest, an_entry_fits_while_the_buffer_holds_it_and_the_crc)
{
    std::array<uint8_t, 16> buffer{};
    fs::CrcRecord record{ infra::MakeRange(buffer) };

    EXPECT_TRUE(record.Fits(8));
    EXPECT_FALSE(record.Fits(9));
    record.Add(5);
    EXPECT_TRUE(record.Fits(3));
    EXPECT_FALSE(record.Fits(4));
}

TEST(CrcRecordTest, a_record_holds_at_most_255_entries)
{
    std::array<uint8_t, 300> buffer{};
    fs::CrcRecord record{ infra::MakeRange(buffer) };

    for (int entry = 0; entry != 255; ++entry)
        record.Add(1);

    EXPECT_FALSE(record.Fits(0));
}

TEST(CrcRecordTest, a_record_is_at_most_0xfffe_bytes)
{
    std::array<uint8_t, 0x10000> buffer{};
    fs::CrcRecord record{ infra::MakeRange(buffer) };

    EXPECT_TRUE(record.Fits(0xfffe - fs::CrcRecord::overhead));
    EXPECT_FALSE(record.Fits(0xffff - fs::CrcRecord::overhead));
}

TEST(CrcRecordTest, opens_a_sealed_record)
{
    auto buffer = TwoEntries();

    auto opened = fs::OpenRecord(infra::MakeRange(buffer));

    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(2u, opened->entries);
    EXPECT_EQ((std::array<uint8_t, 3>{ 0x11, 0x22, 0x33 }), opened->payload);
    EXPECT_EQ(11u, opened->size);
}

TEST(CrcRecordTest, length_comes_from_the_header)
{
    auto buffer = TwoEntries();

    EXPECT_EQ(std::optional<std::size_t>(11), fs::RecordLength(infra::Head(infra::MakeRange(buffer), fs::CrcRecord::headerSize)));
}

TEST(CrcRecordTest, erased_flash_has_no_length)
{
    std::array<uint8_t, 4> header{ 0xff, 0xff, 0xff, 0xff };

    EXPECT_EQ(std::nullopt, fs::RecordLength(infra::MakeRange(header)));
}

TEST(CrcRecordTest, a_length_shorter_than_header_and_crc_is_not_valid)
{
    std::array<uint8_t, 4> header{ 0x07, 0x00, 0x00, 0x00 };

    EXPECT_EQ(std::nullopt, fs::RecordLength(infra::MakeRange(header)));
}

TEST(CrcRecordTest, a_record_with_a_changed_byte_does_not_open)
{
    for (std::size_t offset = 2; offset != 11; ++offset)
    {
        auto buffer = TwoEntries();
        buffer[offset] ^= 0x01;

        EXPECT_EQ(std::nullopt, fs::OpenRecord(infra::MakeRange(buffer))) << offset;
    }
}

TEST(CrcRecordTest, a_record_cut_short_does_not_open)
{
    auto buffer = TwoEntries();

    EXPECT_EQ(std::nullopt, fs::OpenRecord(infra::Head(infra::MakeRange(buffer), 10)));
}

TEST(CrcRecordTest, erased_flash_does_not_open)
{
    std::array<uint8_t, 16> buffer;
    buffer.fill(0xff);

    EXPECT_EQ(std::nullopt, fs::OpenRecord(infra::MakeRange(buffer)));
}
