#include "fs/fat/FatDirectoryEntry.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace
{
    using Entry = std::array<uint8_t, 32>;

    Entry Bytes(std::initializer_list<uint8_t> bytes)
    {
        Entry entry{};
        std::size_t index = 0;

        for (auto byte : bytes)
            entry[index++] = byte;

        return entry;
    }

    const Entry lastLongEntry = Bytes({ 0x42, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x2e, 0x00, 0x74, 0x00, 0x0f, 0x00, 0x02, 0x78, 0x00,
        0x74, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff });
    const Entry firstLongEntry = Bytes({ 0x01, 0x41, 0x00, 0x20, 0x00, 0x6c, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x0f, 0x00, 0x02, 0x67, 0x00,
        0x20, 0x00, 0x66, 0x00, 0x69, 0x00, 0x6c, 0x00, 0x65, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6e, 0x00 });
    const Entry aliasEntry = Bytes({ 0x41, 0x4c, 0x4f, 0x4e, 0x47, 0x46, 0x7e, 0x31, 0x54, 0x58, 0x54, 0x20, 0x00, 0x00, 0xcb, 0x69,
        0x4c, 0x5d, 0x4c, 0x5d, 0x00, 0x00, 0xcb, 0x69, 0x4c, 0x5d, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00 });
    const Entry lowerCaseEntry = Bytes({ 0x52, 0x45, 0x41, 0x44, 0x4d, 0x45, 0x20, 0x20, 0x54, 0x58, 0x54, 0x20, 0x18, 0x00, 0xcb, 0x69,
        0x4c, 0x5d, 0x4c, 0x5d, 0x00, 0x00, 0xcb, 0x69, 0x4c, 0x5d, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00 });

    const infra::BoundedConstString longName{ "A long file name.txt" };

    fs::EntryKind Kind(const Entry& entry)
    {
        return fs::KindOf(infra::MakeRange(entry));
    }

    Entry EncodeLong(infra::BoundedConstString name, uint8_t ordinal, bool last, uint8_t checksum)
    {
        Entry entry;
        entry.fill(0xcc);
        fs::EncodeLongEntry(name, ordinal, last, checksum, infra::MakeRange(entry));
        return entry;
    }

    std::array<char16_t, 13> Units(std::initializer_list<char16_t> units)
    {
        std::array<char16_t, 13> result{};
        std::size_t index = 0;

        for (auto unit : units)
            result[index++] = unit;

        return result;
    }
}

TEST(FatDirectoryEntryTest, decodes_a_short_entry_written_by_mtools)
{
    auto entry = fs::DecodeShortEntry(infra::MakeRange(aliasEntry));

    EXPECT_EQ((std::array<uint8_t, 11>{ 'A', 'L', 'O', 'N', 'G', 'F', '~', '1', 'T', 'X', 'T' }), entry.name.bytes);
    EXPECT_EQ(0, entry.name.caseFlags);
    EXPECT_EQ(fs::FatAttributes::archive, entry.attributes);
    EXPECT_EQ(0x69cb, entry.createTime);
    EXPECT_EQ(0x5d4c, entry.createDate);
    EXPECT_EQ(0x5d4c, entry.accessDate);
    EXPECT_EQ(0x69cb, entry.writeTime);
    EXPECT_EQ(0x5d4c, entry.writeDate);
    EXPECT_EQ(2u, entry.firstCluster);
    EXPECT_EQ(3u, entry.size);
}

TEST(FatDirectoryEntryTest, decodes_lower_case_flags)
{
    EXPECT_EQ(0x18, fs::DecodeShortEntry(infra::MakeRange(lowerCaseEntry)).name.caseFlags);
}

TEST(FatDirectoryEntryTest, ignores_other_bits_of_the_case_byte)
{
    auto entry = lowerCaseEntry;
    entry[12] = 0xff;

    EXPECT_EQ(0x18, fs::DecodeShortEntry(infra::MakeRange(entry)).name.caseFlags);
}

TEST(FatDirectoryEntryTest, first_cluster_combines_high_and_low_words)
{
    auto entry = aliasEntry;
    entry[20] = 0x34;
    entry[21] = 0x12;

    EXPECT_EQ(0x12340002u, fs::DecodeShortEntry(infra::MakeRange(entry)).firstCluster);
}

TEST(FatDirectoryEntryTest, short_entry_round_trip)
{
    Entry entry;
    entry.fill(0xcc);

    fs::EncodeShortEntry(fs::DecodeShortEntry(infra::MakeRange(aliasEntry)), infra::MakeRange(entry));
    EXPECT_EQ(aliasEntry, entry);

    fs::EncodeShortEntry(fs::DecodeShortEntry(infra::MakeRange(lowerCaseEntry)), infra::MakeRange(entry));
    EXPECT_EQ(lowerCaseEntry, entry);
}

TEST(FatDirectoryEntryTest, encodes_the_high_word_of_the_first_cluster)
{
    fs::ShortEntry decoded;
    decoded.firstCluster = 0x0abcdef0;
    Entry entry;

    fs::EncodeShortEntry(decoded, infra::MakeRange(entry));

    EXPECT_EQ(decoded, fs::DecodeShortEntry(infra::MakeRange(entry)));
    EXPECT_EQ(0xbc, entry[20]);
    EXPECT_EQ(0x0a, entry[21]);
}

TEST(FatDirectoryEntryTest, decodes_long_entries_written_by_mtools)
{
    EXPECT_EQ((fs::LongEntry{ 2, true, 0x02, Units({ 'a', 'm', 'e', '.', 't', 'x', 't', 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff }) }), fs::DecodeLongEntry(infra::MakeRange(lastLongEntry)));
    EXPECT_EQ((fs::LongEntry{ 1, false, 0x02, Units({ 'A', ' ', 'l', 'o', 'n', 'g', ' ', 'f', 'i', 'l', 'e', ' ', 'n' }) }), fs::DecodeLongEntry(infra::MakeRange(firstLongEntry)));
}

TEST(FatDirectoryEntryTest, encodes_long_entries_as_mtools)
{
    EXPECT_EQ(firstLongEntry, EncodeLong(longName, 1, false, 0x02));
    EXPECT_EQ(lastLongEntry, EncodeLong(longName, 2, true, 0x02));
}

TEST(FatDirectoryEntryTest, long_name_of_a_multiple_of_13_units_has_no_terminator)
{
    auto entry = EncodeLong("abcdefghijklm", 1, true, 0);

    EXPECT_EQ((fs::LongEntry{ 1, true, 0, Units({ 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm' }) }), fs::DecodeLongEntry(infra::MakeRange(entry)));
}

TEST(FatDirectoryEntryTest, surrogate_pair_is_split_across_entries)
{
    const infra::BoundedConstString name{ "abcdefghijkl\xf0\x9f\x98\x80" };

    EXPECT_EQ(char16_t{ 0xd83d }, fs::DecodeLongEntry(infra::MakeRange(EncodeLong(name, 1, false, 0)))->name[12]);
    EXPECT_EQ(char16_t{ 0xde00 }, fs::DecodeLongEntry(infra::MakeRange(EncodeLong(name, 2, true, 0)))->name[0]);
    EXPECT_EQ(char16_t{ 0x0000 }, fs::DecodeLongEntry(infra::MakeRange(EncodeLong(name, 2, true, 0)))->name[1]);
}

TEST(FatDirectoryEntryTest, rejects_long_entries_with_invalid_ordinals)
{
    for (uint8_t ordinal : std::initializer_list<uint8_t>{ 0x00, 0x40, 0x15, 0x55 })
    {
        auto entry = firstLongEntry;
        entry[0] = ordinal;

        EXPECT_EQ(std::nullopt, fs::DecodeLongEntry(infra::MakeRange(entry))) << int{ ordinal };
    }
}

TEST(FatDirectoryEntryTest, accepts_the_highest_ordinal)
{
    auto entry = firstLongEntry;
    entry[0] = 0x54;

    EXPECT_EQ(20, fs::DecodeLongEntry(infra::MakeRange(entry))->ordinal);
}

TEST(FatDirectoryEntryTest, rejects_long_entries_with_a_type)
{
    auto entry = firstLongEntry;
    entry[12] = 0x01;

    EXPECT_EQ(std::nullopt, fs::DecodeLongEntry(infra::MakeRange(entry)));
}

TEST(FatDirectoryEntryTest, short_entry_is_not_a_long_entry)
{
    EXPECT_EQ(std::nullopt, fs::DecodeLongEntry(infra::MakeRange(aliasEntry)));
}

TEST(FatDirectoryEntryTest, classifies_entries)
{
    EXPECT_EQ(fs::EntryKind::longName, Kind(firstLongEntry));
    EXPECT_EQ(fs::EntryKind::file, Kind(aliasEntry));
    EXPECT_EQ(fs::EntryKind::end, Kind(Entry{}));

    auto entry = aliasEntry;
    entry[0] = 0xe5;
    EXPECT_EQ(fs::EntryKind::free, Kind(entry));

    entry = firstLongEntry;
    entry[0] = 0xe5;
    EXPECT_EQ(fs::EntryKind::free, Kind(entry));

    entry = aliasEntry;
    entry[11] = fs::FatAttributes::directory;
    EXPECT_EQ(fs::EntryKind::directory, Kind(entry));

    entry[0] = '.';
    EXPECT_EQ(fs::EntryKind::dot, Kind(entry));

    entry = aliasEntry;
    entry[11] = fs::FatAttributes::volumeId | fs::FatAttributes::archive;
    EXPECT_EQ(fs::EntryKind::volumeLabel, Kind(entry));

    entry = aliasEntry;
    entry[11] = fs::FatAttributes::longName | 0xc0;
    EXPECT_EQ(fs::EntryKind::longName, Kind(entry));

    entry[11] = fs::FatAttributes::longName | fs::FatAttributes::archive;
    EXPECT_EQ(fs::EntryKind::volumeLabel, Kind(entry));

    entry = aliasEntry;
    entry[11] = fs::FatAttributes::readOnly | fs::FatAttributes::hidden | fs::FatAttributes::system;
    EXPECT_EQ(fs::EntryKind::file, Kind(entry));
}

TEST(FatDirectoryEntryTest, entries_needed_for_a_name)
{
    EXPECT_EQ(1, fs::LongEntriesFor("a"));
    EXPECT_EQ(1, fs::LongEntriesFor("abcdefghijklm"));
    EXPECT_EQ(2, fs::LongEntriesFor("abcdefghijklmn"));
    EXPECT_EQ(2, fs::LongEntriesFor("abcdefghijkl\xf0\x9f\x98\x80"));
    EXPECT_EQ(2, fs::LongEntriesFor(longName));
}
