#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>

namespace
{
    class FatLookupTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatLookupTest(uint32_t blocks = 16384)
            : FatFileSystemFixture(blocks)
        {}

        void SetUp() override
        {
            ExpectMount();
        }

        std::array<uint8_t, 5> hello{ 'h', 'e', 'l', 'l', 'o' };
    };

    class FatLookupOnFat32Test
        : public FatLookupTest
    {
    public:
        FatLookupOnFat32Test()
            : FatLookupTest(81920)
        {}
    };
}

TEST_F(FatLookupTest, stat_of_a_file_reports_its_long_name_and_size)
{
    builder->AddFile(0, "Hello World.txt", "HELLOW~1TXT", infra::MakeRange(hello));

    ExpectStat("/Hello World.txt", fs::Result::success, fs::EntryInfo{ "Hello World.txt", fs::EntryType::file, 5 });
}

TEST_F(FatLookupTest, stat_of_a_directory_has_no_size)
{
    builder->AddDirectory(0, "Documents", "DOCUME~1   ");

    ExpectStat("/Documents", fs::Result::success, fs::EntryInfo{ "Documents", fs::EntryType::directory, 0 });
}

TEST_F(FatLookupTest, ascii_letters_match_in_any_case)
{
    builder->AddFile(0, "Hello World.txt", "HELLOW~1TXT", infra::MakeRange(hello));
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());

    ExpectStat("/hELLO wORLD.TXT", fs::Result::success, fs::EntryInfo{ "Hello World.txt", fs::EntryType::file, 5 });
    ExpectStat("/readme.txt", fs::Result::success, fs::EntryInfo{ "README.TXT", fs::EntryType::file, 0 });
}

TEST_F(FatLookupTest, other_letters_match_only_byte_by_byte)
{
    builder->AddFile(0, "\xc3\x84rger", "RGER~1     ", infra::ConstByteRange());

    ExpectStat("/\xc3\x84rger", fs::Result::success, fs::EntryInfo{ "\xc3\x84rger", fs::EntryType::file, 0 });
    ExpectStat("/\xc3\xa4rger", fs::Result::notFound);
}

TEST_F(FatLookupTest, short_alias_of_a_long_name_matches)
{
    builder->AddFile(0, "Hello World.txt", "HELLOW~1TXT", infra::MakeRange(hello));

    ExpectStat("/HELLOW~1.TXT", fs::Result::success, fs::EntryInfo{ "Hello World.txt", fs::EntryType::file, 5 });
    ExpectStat("/hellow~1.txt", fs::Result::success, fs::EntryInfo{ "Hello World.txt", fs::EntryType::file, 5 });
}

TEST_F(FatLookupTest, path_descends_into_directories)
{
    auto documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
    auto letters = builder->AddDirectory(documents, "Letters", "LETTERS    ");
    builder->AddFile(letters, "to a friend.txt", "TOAFRI~1TXT", infra::MakeRange(hello));

    ExpectStat("/documents/letters/TO A FRIEND.TXT", fs::Result::success, fs::EntryInfo{ "to a friend.txt", fs::EntryType::file, 5 });
    ExpectStat("/Documents/Letters", fs::Result::success, fs::EntryInfo{ "Letters", fs::EntryType::directory, 0 });
}

TEST_F(FatLookupTest, missing_entry_is_not_found)
{
    builder->AddDirectory(0, "Documents", "DOCUME~1   ");

    ExpectStat("/missing", fs::Result::notFound);
    ExpectStat("/Documents/missing", fs::Result::notFound);
    ExpectStat("/missing/file", fs::Result::notFound);
}

TEST_F(FatLookupTest, file_inside_a_path_is_not_a_directory)
{
    builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(hello));

    ExpectStat("/notes.txt/inner", fs::Result::notADirectory);
}

TEST_F(FatLookupTest, component_that_fat_cannot_hold_is_invalid)
{
    ExpectStat("/a*b", fs::Result::invalidPath);
    ExpectStat("/name.", fs::Result::invalidPath);
    ExpectStat("/name ", fs::Result::invalidPath);
}

TEST_F(FatLookupTest, component_longer_than_the_name_limit_is_too_long)
{
    std::array<char, 257> path;
    path.fill('a');
    path[0] = '/';

    ExpectStat(infra::BoundedConstString(path.data(), path.size()), fs::Result::nameTooLong);
}

TEST_F(FatLookupTest, directory_pointing_outside_the_volume_is_corrupted)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'B', 'R', 'O', 'K', 'E', 'N', ' ', ' ', ' ', ' ', ' ' };
    entry.attributes = fs::FatAttributes::directory;
    entry.firstCluster = 1;
    builder->AddEntries(0, "", entry);

    ExpectStat("/broken/file", fs::Result::corrupted);
    ExpectStat("/broken", fs::Result::success, fs::EntryInfo{ "BROKEN", fs::EntryType::directory, 0 });
}

TEST_F(FatLookupTest, device_error_is_reported_and_the_volume_stays_mounted)
{
    builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(hello));
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectStat("/notes.txt", fs::Result::deviceError);
    ExpectStat("/notes.txt", fs::Result::success, fs::EntryInfo{ "notes.txt", fs::EntryType::file, 5 });
}

TEST_F(FatLookupTest, removed_card_faults_the_volume)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ExpectStat("/notes.txt", fs::Result::mediaRemoved);
    ExpectStat("/", fs::Result::mediaRemoved);
    ExpectUnmount(fs::Result::mediaRemoved);

    ExpectMount();
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatLookupTest, call_waiting_behind_a_removal_reports_the_removal)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    StartStat("/second");
    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{})).Times(2);
    ExecuteAllActions();
}

TEST_F(FatLookupTest, lookup_reads_each_directory_sector_once)
{
    auto documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
    builder->AddFile(documents, "notes.txt", "NOTES   TXT", infra::MakeRange(hello));
    auto reads = device.ReadRequests();

    ExpectStat("/Documents/notes.txt", fs::Result::success, fs::EntryInfo{ "notes.txt", fs::EntryType::file, 5 });

    EXPECT_EQ(reads + 2, device.ReadRequests());
}

TEST_F(FatLookupOnFat32Test, root_of_fat32_is_searched_through_its_chain)
{
    builder->AddFile(0, "in the root.txt", "INTHER~1TXT", infra::MakeRange(hello));

    ExpectStat("/in the root.txt", fs::Result::success, fs::EntryInfo{ "in the root.txt", fs::EntryType::file, 5 });
}
