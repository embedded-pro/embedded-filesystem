#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/reader/test/FatReaderFixture.hpp"

namespace
{
    class FatReaderDirectoryTest
        : public FatReaderEveryTypeTest
    {};

    class FatReaderDirectoryFat16Test
        : public testing::Test
        , public FatReaderFixture
    {};

    fs::ShortEntry EntryOf(const char (&name)[12], uint8_t attributes)
    {
        fs::ShortEntry entry;
        std::copy_n(name, entry.name.bytes.size(), entry.name.bytes.begin());
        entry.attributes = attributes;
        return entry;
    }
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatReaderDirectoryTest, testing::Values(fat12Volume, fat16Volume, fat32Volume));

TEST_P(FatReaderDirectoryTest, the_root_lists_its_entries_and_stays_at_its_end)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "First file with a long name.txt", "FIRSTF~1TXT", infra::MakeRange(contents));
    builder->AddDirectory(0, "", "SUB        ");
    builder->AddFile(0, "", "SHORT   TXT", infra::Head(infra::MakeRange(contents), 10));
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "First file with a long name.txt", fs::EntryType::file, 100 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "SUB", fs::EntryType::directory, 0 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "SHORT.TXT", fs::EntryType::file, 10 });
    ExpectNext(*directory, fs::Result::success);
    ExpectNext(*directory, fs::Result::success);
}

TEST_P(FatReaderDirectoryTest, a_subdirectory_lists_its_entries_without_dot_entries)
{
    auto contents = Pattern<100>();
    auto documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
    builder->AddFile(documents, "Quarterly report.pdf", "QUARTE~1PDF", infra::MakeRange(contents));
    ExpectMount();
    auto directory = ExpectOpenDirectory("/documents");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "Quarterly report.pdf", fs::EntryType::file, 100 });
    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatReaderDirectoryFat16Test, a_directory_is_listed_across_its_clusters)
{
    auto sub = builder->AddDirectory(0, "", "SUB        ");
    std::array<char, 12> name{ 'F', 'I', 'L', 'E', ' ', ' ', ' ', ' ', 'T', 'X', 'T', 0 };
    for (char index = 0; index != 20; ++index)
    {
        name[4] = static_cast<char>('A' + index);
        builder->AddFile(sub, "", reinterpret_cast<const char(&)[12]>(*name.data()), infra::ConstByteRange());
    }
    ExpectMount();
    auto directory = ExpectOpenDirectory("/sub");

    for (char index = 0; index != 20; ++index)
    {
        std::array<char, 9> expected{ 'F', 'I', 'L', 'E', static_cast<char>('A' + index), '.', 'T', 'X', 'T' };
        ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ infra::BoundedConstString(expected.data(), expected.size()), fs::EntryType::file, 0 });
    }

    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatReaderDirectoryFat16Test, listing_skips_free_entries_volume_labels_and_orphaned_long_names)
{
    builder->AddEntries(0, "", EntryOf("CARD       ", fs::FatAttributes::volumeId | fs::FatAttributes::archive));
    auto removed = builder->AddEntries(0, "", EntryOf("REMOVED TXT", fs::FatAttributes::archive));
    std::array<uint8_t, 32> raw{};
    fs::EncodeShortEntry(EntryOf("REMOVED TXT", fs::FatAttributes::archive), infra::MakeRange(raw));
    raw[0] = 0xe5;
    builder->WriteRawEntry(removed, infra::MakeRange(raw));
    auto orphan = builder->AddEntries(0, "Orphaned long name.txt", EntryOf("ORPHAN~1TXT", fs::FatAttributes::archive));
    fs::EncodeLongEntry("Orphaned long name.txt", 2, true, 0x12, infra::MakeRange(raw));
    builder->WriteRawEntry(fs::FatEntryPosition{ orphan.cluster, orphan.index - 2 }, infra::MakeRange(raw));
    builder->AddEntries(0, "", EntryOf("readme  txt", fs::FatAttributes::archive));
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "ORPHAN~1.TXT", fs::EntryType::file, 0 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "readme.txt", fs::EntryType::file, 0 });
    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatReaderDirectoryFat16Test, short_names_are_read_with_their_lower_case_flags_and_code_page_437)
{
    auto entry = EntryOf("READ    ME ", fs::FatAttributes::archive);
    entry.name.caseFlags = fs::ShortName::lowerCaseBase;
    builder->AddEntries(0, "", entry);
    auto accented = EntryOf("CAF\x82    TXT", fs::FatAttributes::archive);
    builder->AddEntries(0, "", accented);
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "read.ME", fs::EntryType::file, 0 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "CAF\xc3\xa9.TXT", fs::EntryType::file, 0 });
    ExpectStat("/read.me", fs::Result::success, fs::EntryInfo{ "read.ME", fs::EntryType::file, 0 });
}

TEST_F(FatReaderDirectoryFat16Test, without_long_names_entries_are_listed_by_their_short_names)
{
    fs::FatReader::WithStorage<1, 1, fs::PathLimits{}, fs::FatReaderFeatures{ .longNames = false }> shortNames{ device };
    builder->AddFile(0, "Meeting notes.txt", "MEETIN~1TXT", infra::ConstByteRange());
    shortNames.Mount(Done());
    ExpectDone(fs::Result::success);
    auto directory = ExpectOpenDirectory(shortNames, "/");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "MEETIN~1.TXT", fs::EntryType::file, 0 });
    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatReaderDirectoryFat16Test, a_second_next_on_a_handle_is_busy)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");

    StartNext(*directory);
    StartNext(*directory);

    EXPECT_CALL(nextDone, callback(fs::Result::busy, std::optional<fs::EntryInfo>()));
    EXPECT_CALL(nextDone, callback(fs::Result::success, std::optional<fs::EntryInfo>(fs::EntryInfo{ "FILE.TXT", fs::EntryType::file, 0 })));
    ExecuteAllActions();
}

TEST_F(FatReaderDirectoryFat16Test, directory_handles_are_not_mounted_after_unmount)
{
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");
    ExpectUnmount();

    ExpectNext(*directory, fs::Result::notMounted);
}

TEST_F(FatReaderDirectoryFat16Test, device_error_while_listing_is_reported)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectNext(*directory, fs::Result::deviceError);
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "FILE.TXT", fs::EntryType::file, 0 });
}

TEST_F(FatReaderDirectoryFat16Test, removed_card_while_listing_faults_the_volume)
{
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ExpectNext(*directory, fs::Result::mediaRemoved);
    ExpectNext(*directory, fs::Result::mediaRemoved);
    ExpectStat("/", fs::Result::mediaRemoved);
}
