#include "fs/nor/NorInodeRecord.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 4096, .inlineBytes = 40, .maxNameBytes = 8 };
    using Record = std::array<uint8_t, 68>;

    constexpr std::array<uint8_t, 3> name{ 'l', 'o', 'g' };
    constexpr std::array<uint8_t, 2> inlineBytes{ 0xaa, 0xbb };

    const fs::NorInodeRecord file{ 1, fs::NorInodeType::file, 2, 5000, 0x12000, infra::MakeRange(inlineBytes), infra::MakeRange(name) };

    Record Serialized(const fs::NorInodeRecord& record)
    {
        Record bytes;
        bytes.fill(0x55);
        fs::SerializeNorInodeRecord(record, infra::MakeRange(bytes), config);
        return bytes;
    }
}

TEST(NorInodeRecordTest, serializes_the_fields_the_inline_area_and_the_name)
{
    auto bytes = Serialized(file);

    EXPECT_EQ((std::array<uint8_t, 20>{ 1, 0, 0, 0, 1, 3, 2, 0, 0x88, 0x13, 0, 0, 0, 0x20, 0x01, 0, 2, 0, 0, 0 }), infra::Head(infra::MakeRange(bytes), 20));
    EXPECT_EQ((std::array<uint8_t, 4>{ 0xaa, 0xbb, 0, 0 }), infra::Head(infra::DiscardHead(infra::MakeRange(bytes), 20), 4));
    EXPECT_EQ((std::array<uint8_t, 8>{ 'l', 'o', 'g', 0, 0, 0, 0, 0 }), infra::DiscardHead(infra::MakeRange(bytes), 60));
}

TEST(NorInodeRecordTest, parses_what_it_serializes)
{
    auto bytes = Serialized(file);

    EXPECT_EQ(file, fs::ParseNorInodeRecord(infra::MakeRange(bytes), config));
}

TEST(NorInodeRecordTest, the_root_directory_has_an_empty_name_and_no_map)
{
    fs::NorInodeRecord root{ 0, fs::NorInodeType::directory };
    auto bytes = Serialized(root);

    auto parsed = fs::ParseNorInodeRecord(infra::MakeRange(bytes), config);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(fs::NorInodeType::directory, parsed->type);
    EXPECT_TRUE(parsed->name.empty());
    EXPECT_TRUE(parsed->inlineBytes.empty());
    EXPECT_EQ(0u, parsed->mapRoot);
}

TEST(NorInodeRecordTest, records_with_different_fields_differ)
{
    std::array<uint8_t, 3> otherName{ 'l', 'o', 'h' };
    auto other = file;
    other.name = infra::MakeRange(otherName);

    EXPECT_NE(file, other);

    using Change = void (*)(fs::NorInodeRecord& record);
    const std::array<Change, 6> changes{ [](fs::NorInodeRecord& record)
        {
            record.parent = 2;
        },
        [](fs::NorInodeRecord& record)
        {
            record.type = fs::NorInodeType::directory;
        },
        [](fs::NorInodeRecord& record)
        {
            record.mapHeight = 1;
        },
        [](fs::NorInodeRecord& record)
        {
            record.size = 1;
        },
        [](fs::NorInodeRecord& record)
        {
            record.mapRoot = 1;
        },
        [](fs::NorInodeRecord& record)
        {
            record.inlineBytes = {};
        } };

    for (auto change : changes)
    {
        other = file;
        change(other);
        EXPECT_NE(file, other);
    }
}

TEST(NorInodeRecordTest, an_unknown_type_is_not_a_record)
{
    for (uint8_t type : { 0, 3 })
    {
        auto bytes = Serialized(file);
        bytes[4] = type;

        EXPECT_EQ(std::nullopt, fs::ParseNorInodeRecord(infra::MakeRange(bytes), config)) << int(type);
    }
}

TEST(NorInodeRecordTest, a_name_longer_than_the_limit_is_not_a_record)
{
    auto bytes = Serialized(file);
    bytes[5] = 9;

    EXPECT_EQ(std::nullopt, fs::ParseNorInodeRecord(infra::MakeRange(bytes), config));
}

TEST(NorInodeRecordTest, inline_bytes_beyond_the_inline_area_are_not_a_record)
{
    auto bytes = Serialized(file);
    bytes[16] = 41;

    EXPECT_EQ(std::nullopt, fs::ParseNorInodeRecord(infra::MakeRange(bytes), config));
}
