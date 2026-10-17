#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatShortName.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    using Entry = std::array<uint8_t, 32>;

    Entry Short(const char (&name)[12], uint8_t caseFlags = 0, uint8_t attributes = fs::FatAttributes::archive)
    {
        fs::ShortEntry entry;
        std::copy_n(name, 11, entry.name.bytes.begin());
        entry.name.caseFlags = caseFlags;
        entry.attributes = attributes;
        entry.size = 7;
        Entry raw;
        fs::EncodeShortEntry(entry, infra::MakeRange(raw));
        return raw;
    }

    uint8_t ChecksumOf(const char (&name)[12])
    {
        std::array<uint8_t, 11> bytes;
        std::copy_n(name, 11, bytes.begin());
        return fs::LongNameChecksum(bytes);
    }

    Entry Long(infra::BoundedConstString name, uint8_t ordinal, bool last, uint8_t checksum)
    {
        Entry raw;
        fs::EncodeLongEntry(name, ordinal, last, checksum, infra::MakeRange(raw));
        return raw;
    }

    class FatEntryAssemblerTest
        : public testing::Test
    {
    public:
        fs::FatEntryAssembler::Outcome Feed(const Entry& entry)
        {
            return assembler.Feed(infra::MakeRange(entry), fs::FatEntryPosition{ 7, index++ });
        }

        void FeedSet(infra::BoundedConstString name, const char (&shortName)[12])
        {
            auto entries = fs::LongEntriesFor(name);

            for (auto ordinal = entries; ordinal != 0; --ordinal)
                EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, Feed(Long(name, ordinal, ordinal == entries, ChecksumOf(shortName))));

            EXPECT_EQ(fs::FatEntryAssembler::Outcome::entry, Feed(Short(shortName)));
        }

        infra::BoundedConstString Name() const
        {
            return assembler.Entry().name;
        }

        std::array<uint8_t, 64> buffer{};
        fs::FatEntryAssembler assembler{ infra::MakeRange(buffer) };
        uint32_t index{ 0 };
    };
}

TEST_F(FatEntryAssemblerTest, short_entry_alone_has_its_short_name)
{
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::entry, Feed(Short("README  TXT")));

    EXPECT_EQ("README.TXT", Name());
    EXPECT_FALSE(assembler.Entry().longName);
    EXPECT_EQ(1, assembler.Entry().slots);
    EXPECT_EQ((fs::FatEntryPosition{ 7, 0 }), assembler.Entry().first);
    EXPECT_EQ((fs::FatEntryPosition{ 7, 0 }), assembler.Entry().shortEntry);
    EXPECT_EQ(7u, assembler.Entry().entry.size);
}

TEST_F(FatEntryAssemblerTest, lower_case_flags_apply_to_the_short_name)
{
    Feed(Short("README  TXT", 0x18));

    EXPECT_EQ("readme.txt", Name());
}

TEST_F(FatEntryAssemblerTest, long_name_from_entries_in_reverse_order)
{
    FeedSet("A long file name.txt", "ALONGF~1TXT");

    EXPECT_EQ("A long file name.txt", Name());
    EXPECT_TRUE(assembler.Entry().longName);
    EXPECT_EQ(3, assembler.Entry().slots);
    EXPECT_EQ((fs::FatEntryPosition{ 7, 0 }), assembler.Entry().first);
    EXPECT_EQ((fs::FatEntryPosition{ 7, 2 }), assembler.Entry().shortEntry);
}

TEST_F(FatEntryAssemblerTest, long_name_of_exactly_thirteen_units)
{
    FeedSet("abcdefghijklm", "ABCDEF~1   ");

    EXPECT_EQ("abcdefghijklm", Name());
}

TEST_F(FatEntryAssemblerTest, long_name_with_non_ascii_characters)
{
    FeedSet("\xc3\x9cn\xc3\xaf"
            "c\xc3\xb8"
            "d\xc3\xa9.dat",
        "_N_C_D~1DAT");

    EXPECT_EQ("\xc3\x9cn\xc3\xaf"
              "c\xc3\xb8"
              "d\xc3\xa9.dat",
        Name());
}

TEST_F(FatEntryAssemblerTest, surrogate_pair_split_across_entries)
{
    FeedSet("abcdefghijkl\xf0\x9f\x98\x80", "ABCDEF~1   ");

    EXPECT_EQ("abcdefghijkl\xf0\x9f\x98\x80", Name());
}

TEST_F(FatEntryAssemblerTest, wrong_checksum_reports_the_short_name)
{
    Feed(Long("long name", 1, true, ChecksumOf("LONGNA~1   ") + 1));
    Feed(Short("LONGNA~1   "));

    EXPECT_EQ("LONGNA~1", Name());
    EXPECT_FALSE(assembler.Entry().longName);
    EXPECT_EQ(1, assembler.Entry().slots);
    EXPECT_EQ((fs::FatEntryPosition{ 7, 1 }), assembler.Entry().first);
}

TEST_F(FatEntryAssemblerTest, set_without_its_last_entry_is_an_orphan)
{
    Feed(Long("long name", 1, false, ChecksumOf("LONGNA~1   ")));
    Feed(Short("LONGNA~1   "));

    EXPECT_EQ("LONGNA~1", Name());
}

TEST_F(FatEntryAssemblerTest, gap_in_the_ordinals_is_an_orphan)
{
    const infra::BoundedConstString name{ "a name that needs three entries......" };
    Feed(Long(name, 3, true, ChecksumOf("ANAMET~1   ")));
    Feed(Long(name, 1, false, ChecksumOf("ANAMET~1   ")));
    Feed(Short("ANAMET~1   "));

    EXPECT_EQ("ANAMET~1", Name());
}

TEST_F(FatEntryAssemblerTest, incomplete_set_reports_the_short_name)
{
    const infra::BoundedConstString name{ "a name of two entries" };
    Feed(Long(name, 2, true, ChecksumOf("ANAMEO~1   ")));
    Feed(Short("ANAMEO~1   "));

    EXPECT_EQ("ANAMEO~1", Name());
}

TEST_F(FatEntryAssemblerTest, entry_of_another_checksum_in_the_middle_is_an_orphan)
{
    const infra::BoundedConstString name{ "a name of two entries" };
    Feed(Long(name, 2, true, ChecksumOf("ANAMEO~1   ")));
    Feed(Long(name, 1, false, ChecksumOf("ANAMEO~1   ") + 1));
    Feed(Short("ANAMEO~1   "));

    EXPECT_EQ("ANAMEO~1", Name());
}

TEST_F(FatEntryAssemblerTest, new_set_replaces_an_unfinished_one)
{
    Feed(Long("abandoned", 1, true, 0x55));
    FeedSet("kept name", "KEPTNA~1   ");

    EXPECT_EQ("kept name", Name());
}

TEST_F(FatEntryAssemblerTest, name_longer_than_the_buffer_reports_the_short_name)
{
    std::array<uint8_t, 40> small{};
    fs::FatEntryAssembler smallAssembler{ infra::MakeRange(small) };
    const infra::BoundedConstString name{ "a name that is longer than forty bytes in UTF-8" };
    auto checksum = ChecksumOf("ANAMET~1   ");

    uint32_t position = 0;
    for (auto ordinal = fs::LongEntriesFor(name); ordinal != 0; --ordinal)
        smallAssembler.Feed(infra::MakeRange(Long(name, ordinal, ordinal == fs::LongEntriesFor(name), checksum)), fs::FatEntryPosition{ 3, position++ });
    smallAssembler.Feed(infra::MakeRange(Short("ANAMET~1   ")), fs::FatEntryPosition{ 3, position });

    EXPECT_EQ("ANAMET~1", smallAssembler.Entry().name);
    EXPECT_FALSE(smallAssembler.Entry().longName);
    EXPECT_EQ(fs::LongEntriesFor(name) + 1, smallAssembler.Entry().slots);
    EXPECT_EQ((fs::FatEntryPosition{ 3, 0 }), smallAssembler.Entry().first);
}

TEST_F(FatEntryAssemblerTest, unpaired_surrogate_reports_the_short_name)
{
    auto entry = Long("ab", 1, true, ChecksumOf("AB~1       "));
    entry[1] = 0x3d;
    entry[2] = 0xd8;
    Feed(entry);
    Feed(Short("AB~1       "));

    EXPECT_EQ("AB~1", Name());
}

TEST_F(FatEntryAssemblerTest, unpaired_low_surrogate_reports_the_short_name)
{
    auto entry = Long("ab", 1, true, ChecksumOf("AB~1       "));
    entry[3] = 0x00;
    entry[4] = 0xde;
    Feed(entry);
    Feed(Short("AB~1       "));

    EXPECT_EQ("AB~1", Name());
}

TEST_F(FatEntryAssemblerTest, terminator_inside_the_name_reports_the_short_name)
{
    const infra::BoundedConstString name{ "abcdefghijklmnop" };
    auto first = Long(name, 1, false, ChecksumOf("ABCDEF~1   "));
    first[3] = 0;
    first[4] = 0;
    Feed(Long(name, 2, true, ChecksumOf("ABCDEF~1   ")));
    Feed(first);
    Feed(Short("ABCDEF~1   "));

    EXPECT_EQ("ABCDEF~1", Name());
}

TEST_F(FatEntryAssemblerTest, free_entry_breaks_a_set)
{
    Feed(Long("long name", 1, true, ChecksumOf("LONGNA~1   ")));
    auto deleted = Short("OLD     TXT");
    deleted[0] = 0xe5;
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, Feed(deleted));
    Feed(Short("LONGNA~1   "));

    EXPECT_EQ("LONGNA~1", Name());
}

TEST_F(FatEntryAssemblerTest, invalid_long_entry_breaks_a_set)
{
    Feed(Long("long name", 1, true, ChecksumOf("LONGNA~1   ")));
    auto invalid = Long("x", 1, true, 0);
    invalid[0] = 0x40;
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, Feed(invalid));
    Feed(Short("LONGNA~1   "));

    EXPECT_EQ("LONGNA~1", Name());
}

TEST_F(FatEntryAssemblerTest, labels_and_dot_entries_are_skipped)
{
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, Feed(Short("VOLUME     ", 0, fs::FatAttributes::volumeId)));
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, Feed(Short(".          ", 0, fs::FatAttributes::directory)));
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, Feed(Short("..         ", 0, fs::FatAttributes::directory)));
}

TEST_F(FatEntryAssemblerTest, end_marker_ends_the_directory)
{
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::end, Feed(Entry{}));
}

TEST_F(FatEntryAssemblerTest, directory_entries_are_reported)
{
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::entry, Feed(Short("SUBDIR     ", 0, fs::FatAttributes::directory)));

    EXPECT_EQ("SUBDIR", Name());
    EXPECT_EQ(fs::FatAttributes::directory, assembler.Entry().entry.attributes);
}

TEST_F(FatEntryAssemblerTest, oem_short_names_decode_as_code_page_437)
{
    Feed(Short("\x9aN\xd8     DAT"));

    EXPECT_EQ("\xc3\x9cN\xe2\x95\xaa.DAT", Name());
}

TEST_F(FatEntryAssemblerTest, without_long_names_a_set_is_its_short_entry)
{
    std::array<uint8_t, fs::ShortName::maxUtf8Bytes> shortBuffer{};
    fs::FatEntryAssembler shortNames{ infra::MakeRange(shortBuffer), &fs::FatEntryAssembler::ReadShortNames };
    auto last = Long("A long file name.txt", 2, true, ChecksumOf("ALONGF~1TXT"));
    auto first = Long("A long file name.txt", 1, false, ChecksumOf("ALONGF~1TXT"));
    auto alias = Short("ALONGF~1TXT");

    EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, shortNames.Feed(infra::MakeRange(last), fs::FatEntryPosition{ 7, 0 }));
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::none, shortNames.Feed(infra::MakeRange(first), fs::FatEntryPosition{ 7, 1 }));
    EXPECT_EQ(fs::FatEntryAssembler::Outcome::entry, shortNames.Feed(infra::MakeRange(alias), fs::FatEntryPosition{ 7, 2 }));

    EXPECT_EQ("ALONGF~1.TXT", shortNames.Entry().name);
    EXPECT_FALSE(shortNames.Entry().longName);
    EXPECT_EQ(1, shortNames.Entry().slots);
    EXPECT_EQ((fs::FatEntryPosition{ 7, 2 }), shortNames.Entry().first);
    EXPECT_EQ((fs::FatEntryPosition{ 7, 2 }), shortNames.Entry().shortEntry);
}

TEST(FatEntryInfoTest, file_reports_its_name_and_size)
{
    fs::FatScannedEntry scanned;
    scanned.entry.attributes = fs::FatAttributes::archive;
    scanned.entry.size = 1234;
    scanned.name = infra::BoundedConstString("notes.txt");

    EXPECT_FALSE(fs::IsDirectory(scanned));
    EXPECT_EQ((fs::EntryInfo{ "notes.txt", fs::EntryType::file, 1234 }), fs::InfoOf(scanned));
}

TEST(FatEntryInfoTest, directory_has_no_size_whatever_its_entry_holds)
{
    fs::FatScannedEntry scanned;
    scanned.entry.attributes = fs::FatAttributes::directory;
    scanned.entry.size = 1234;
    scanned.name = infra::BoundedConstString("Documents");

    EXPECT_TRUE(fs::IsDirectory(scanned));
    EXPECT_EQ((fs::EntryInfo{ "Documents", fs::EntryType::directory, 0 }), fs::InfoOf(scanned));
}
