#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace
{
    fs::FatConfig LongPaths()
    {
        fs::FatConfig config;
        config.maxPathBytes = 300;
        return config;
    }

    class FatRenameTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatRenameTest(uint32_t blocks = 16384, const fs::FatFormatConfig& format = ClusterSize(1))
            : FatFileSystemFixture(blocks, format, LongPaths())
        {
            for (std::size_t index = 0; index != notes.size(); ++index)
                notes[index] = static_cast<uint8_t>(index * 5 + 1);
        }

        void SetUp() override
        {
            notesCluster = builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(notes));
            documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
            builder->AddFile(documents, "", "REPORT  TXT", infra::MakeRange(notes));
            sub = builder->AddDirectory(documents, "", "SUB        ");
            ExpectMount();
        }

        void ExpectRename(infra::BoundedConstString from, infra::BoundedConstString to, fs::Result result = fs::Result::success)
        {
            volume.Rename(from, to, [this](fs::Result result)
                {
                    done.callback(result);
                });
            ExpectDone(result);
        }

        void RecordRequests()
        {
            device.Observe([this](fs::SparseBlockDevice::Request request, uint32_t block)
                {
                    if (request != fs::SparseBlockDevice::Request::read)
                        requests.emplace_back(request, block);
                });
        }

        fs::ShortEntry RawEntry(uint32_t cluster, uint32_t index) const
        {
            auto sector = builder->ReadSector(builder->SectorOf(fs::FatEntryPosition{ cluster, index }));
            return fs::DecodeShortEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), index % 16 * 32), 32));
        }

        uint32_t ParentOf(uint32_t directory) const
        {
            return RawEntry(directory, 1).firstCluster;
        }

        void ExpectNotes(infra::BoundedConstString path)
        {
            auto file = ExpectOpen(path);
            ASSERT_NE(nullptr, file);
            std::array<uint8_t, 701> read{};
            ExpectRead(*file, infra::MakeRange(read), fs::Result::success, 700);
            EXPECT_TRUE(std::equal(notes.begin(), notes.end(), read.begin()));
        }

        void Remount()
        {
            ExpectUnmount();
            ExpectMount();
        }

        std::array<uint8_t, 700> notes{};
        uint32_t notesCluster{ 0 };
        uint32_t documents{ 0 };
        uint32_t sub{ 0 };
        infra::BoundedVector<std::pair<fs::SparseBlockDevice::Request, uint32_t>>::WithMaxSize<32> requests;
    };

    class FatRenameWithSmallRootTest
        : public FatRenameTest
    {
    public:
        FatRenameWithSmallRootTest()
            : FatRenameTest(16384, SmallRoot())
        {}

        static fs::FatFormatConfig SmallRoot()
        {
            auto config = ClusterSize(1);
            config.rootEntries = 16;
            return config;
        }
    };

    class FatRenameOnFat32Test
        : public FatRenameTest
    {
    public:
        FatRenameOnFat32Test()
            : FatRenameTest(81920)
        {}
    };

    using Request = fs::SparseBlockDevice::Request;
}

TEST_F(FatRenameTest, renamed_file_is_found_under_its_new_name_only)
{
    ExpectRename("/notes.txt", "/memo.txt");

    ExpectStat("/notes.txt", fs::Result::notFound);
    ExpectStat("/memo.txt", fs::Result::success, fs::EntryInfo{ "memo.txt", fs::EntryType::file, 700 });
    Remount();
    ExpectNotes("/memo.txt");
    EXPECT_EQ(notesCluster + 1, builder->FatEntry(notesCluster));
}

TEST_F(FatRenameTest, new_entry_reaches_the_device_before_the_old_entry_is_cleared)
{
    RecordRequests();

    ExpectRename("/notes.txt", "/memo.txt");

    auto root = builder->Geometry().rootSector;
    ASSERT_EQ(4u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, root), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ(std::make_pair(Request::write, root), requests[2]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[3]);
}

TEST_F(FatRenameTest, file_moves_into_another_directory)
{
    ExpectRename("/notes.txt", "/Documents/notes.txt");

    ExpectStat("/notes.txt", fs::Result::notFound);
    Remount();
    ExpectNotes("/documents/NOTES.TXT");
}

TEST_F(FatRenameTest, directory_moved_to_another_parent_points_its_dot_dot_at_the_new_parent)
{
    ExpectRename("/Documents/SUB", "/Moved");

    EXPECT_EQ(0u, ParentOf(sub));
    ExpectStat("/Documents/SUB", fs::Result::notFound);
    ExpectStat("/Moved", fs::Result::success, fs::EntryInfo{ "Moved", fs::EntryType::directory, 0 });

    ExpectRename("/Moved", "/Documents/Back");

    EXPECT_EQ(documents, ParentOf(sub));
}

TEST_F(FatRenameTest, directory_renamed_within_its_parent_keeps_its_dot_dot)
{
    RecordRequests();

    ExpectRename("/Documents/SUB", "/Documents/OTHER");

    EXPECT_EQ(documents, ParentOf(sub));
    EXPECT_TRUE(std::none_of(requests.begin(), requests.end(), [this](const auto& request)
        {
            return request.second == builder->Geometry().SectorOfCluster(sub);
        }));
}

TEST_F(FatRenameTest, moved_directory_gets_its_new_entry_first_and_its_dot_dot_with_the_cleared_entry)
{
    RecordRequests();

    ExpectRename("/Documents/SUB", "/SUB");

    const auto& geometry = builder->Geometry();
    ASSERT_EQ(5u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_NE(requests.end(), std::find(requests.begin() + 2, requests.end(), std::make_pair(Request::write, geometry.SectorOfCluster(documents))));
    EXPECT_NE(requests.end(), std::find(requests.begin() + 2, requests.end(), std::make_pair(Request::write, geometry.SectorOfCluster(sub))));
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[4]);
}

TEST_F(FatRenameTest, rename_keeps_the_entry_but_its_name)
{
    auto original = RawEntry(0, 1);

    ExpectRename("/notes.txt", "/memo.txt");

    auto renamed = RawEntry(0, 4);
    fs::ShortNameBytes name{ 'M', 'E', 'M', 'O', ' ', ' ', ' ', ' ', 'T', 'X', 'T' };
    EXPECT_EQ(name, renamed.name.bytes);
    EXPECT_EQ(notesCluster, renamed.firstCluster);
    EXPECT_EQ(original.size, renamed.size);
    EXPECT_EQ(original.attributes, renamed.attributes);
    EXPECT_EQ(original.createDate, renamed.createDate);
    EXPECT_EQ(original.writeTime, renamed.writeTime);
}

TEST_F(FatRenameTest, existing_target_is_already_exists)
{
    ExpectRename("/notes.txt", "/documents", fs::Result::alreadyExists);
    ExpectRename("/notes.txt", "/Documents/report.txt", fs::Result::alreadyExists);
    ExpectNotes("/notes.txt");
}

TEST_F(FatRenameTest, missing_source_or_target_parent_is_not_found)
{
    ExpectRename("/missing.txt", "/memo.txt", fs::Result::notFound);
    ExpectRename("/notes.txt", "/missing/memo.txt", fs::Result::notFound);
}

TEST_F(FatRenameTest, root_cannot_be_renamed_or_replaced)
{
    ExpectRename("/", "/other", fs::Result::invalidPath);
    ExpectRename("/notes.txt", "/", fs::Result::invalidPath);
}

TEST_F(FatRenameTest, directory_cannot_move_into_its_own_subtree)
{
    ExpectRename("/Documents", "/Documents/inner", fs::Result::invalidPath);
    ExpectRename("/Documents", "/Documents/SUB/inner", fs::Result::invalidPath);
    ExpectRename("/Documents", "/DOCUME~1/SUB/inner", fs::Result::invalidPath);
    ExpectStat("/Documents", fs::Result::success, fs::EntryInfo{ "Documents", fs::EntryType::directory, 0 });
}

TEST_F(FatRenameTest, file_can_move_next_to_a_directory_of_the_same_subtree)
{
    ExpectRename("/Documents/REPORT.TXT", "/Documents/SUB/report.txt");

    ExpectStat("/Documents/SUB/report.txt", fs::Result::success, fs::EntryInfo{ "report.txt", fs::EntryType::file, 700 });
}

TEST_F(FatRenameTest, target_under_a_file_is_not_a_directory)
{
    ExpectRename("/Documents/REPORT.TXT", "/notes.txt/report.txt", fs::Result::notADirectory);
}

TEST_F(FatRenameTest, invalid_target_names_are_rejected)
{
    ExpectRename("/notes.txt", "/a*b.txt", fs::Result::invalidPath);
    ExpectRename("/notes.txt", "memo.txt", fs::Result::invalidPath);

    std::array<char, 258> longName;
    longName.fill('n');
    longName[0] = '/';
    ExpectRename("/notes.txt", infra::BoundedConstString(longName.data(), longName.size()), fs::Result::nameTooLong);
}

TEST_F(FatRenameTest, open_file_or_directory_is_busy)
{
    auto file = ExpectOpen("/notes.txt");
    auto directory = ExpectOpenDirectory("/Documents/SUB");

    ExpectRename("/notes.txt", "/memo.txt", fs::Result::busy);
    ExpectRename("/Documents/SUB", "/SUB", fs::Result::busy);
}

TEST_F(FatRenameTest, read_only_file_can_be_renamed)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'L', 'O', 'C', 'K', 'E', 'D', ' ', ' ', ' ', ' ', ' ' };
    entry.attributes = fs::FatAttributes::readOnly;
    builder->AddEntries(0, "", entry);

    ExpectRename("/LOCKED", "/KEPT");

    ExpectStat("/KEPT", fs::Result::success, fs::EntryInfo{ "KEPT", fs::EntryType::file, 0 });
}

TEST_F(FatRenameTest, entry_with_a_cluster_outside_the_volume_is_corrupted)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'B', 'R', 'O', 'K', 'E', 'N', ' ', ' ', ' ', ' ', ' ' };
    entry.firstCluster = 0x0fffff00;
    entry.size = 10;
    builder->AddEntries(0, "", entry);

    ExpectRename("/BROKEN", "/FIXED", fs::Result::corrupted);
}

TEST_F(FatRenameTest, renaming_to_the_same_name_writes_nothing)
{
    RecordRequests();

    ExpectRename("/notes.txt", "/notes.txt");

    EXPECT_TRUE(requests.empty());
}

TEST_F(FatRenameTest, case_only_rename_changes_the_listed_name)
{
    ExpectRename("/notes.txt", "/NOTES.TXT");

    ExpectStat("/notes.txt", fs::Result::success, fs::EntryInfo{ "NOTES.TXT", fs::EntryType::file, 700 });
    Remount();
    ExpectNotes("/NOTES.TXT");
}

TEST_F(FatRenameTest, long_target_name_gets_a_short_alias)
{
    ExpectRename("/notes.txt", "/Meeting notes of October.txt");

    ExpectStat("/MEETIN~1.TXT", fs::Result::success, fs::EntryInfo{ "Meeting notes of October.txt", fs::EntryType::file, 700 });
}

TEST_F(FatRenameTest, moved_directory_without_a_dot_dot_entry_is_corrupted)
{
    std::array<uint8_t, 32> raw{};
    raw.fill(' ');
    raw[11] = fs::FatAttributes::directory;
    builder->WriteRawEntry(fs::FatEntryPosition{ sub, 1 }, infra::MakeRange(raw));

    ExpectRename("/Documents/SUB", "/SUB", fs::Result::corrupted);
}

TEST_F(FatRenameTest, failed_write_of_the_new_entry_keeps_the_old_one)
{
    ExpectStat("/notes.txt", fs::Result::success, fs::EntryInfo{ "notes.txt", fs::EntryType::file, 700 });
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectRename("/notes.txt", "/memo.txt", fs::Result::deviceError);

    ExpectStat("/notes.txt", fs::Result::success, fs::EntryInfo{ "notes.txt", fs::EntryType::file, 700 });
}

TEST_F(FatRenameTest, failed_final_flush_is_reported)
{
    ExpectStat("/notes.txt", fs::Result::success, fs::EntryInfo{ "notes.txt", fs::EntryType::file, 700 });
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 2);

    ExpectRename("/notes.txt", "/memo.txt", fs::Result::deviceError);
}

TEST_F(FatRenameTest, failed_read_of_the_moved_directory_is_reported)
{
    ExpectStat("/Documents/REPORT.TXT", fs::Result::success, fs::EntryInfo{ "REPORT.TXT", fs::EntryType::file, 700 });
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 2);

    ExpectRename("/Documents/SUB", "/SUB", fs::Result::deviceError);
}

TEST_F(FatRenameTest, failed_scan_of_the_source_parent_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectRename("/notes.txt", "/memo.txt", fs::Result::deviceError);
}

TEST_F(FatRenameTest, rename_completes_after_the_event_loop_runs)
{
    volume.Rename("/notes.txt", "/memo.txt", [this](fs::Result result)
        {
            done.callback(result);
        });

    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatRenameTest, rename_after_unmount_is_not_mounted)
{
    ExpectUnmount();

    ExpectRename("/notes.txt", "/memo.txt", fs::Result::notMounted);
}

TEST_F(FatRenameTest, invalid_source_path_is_rejected_before_it_is_queued)
{
    ExpectRename("relative", "/memo.txt", fs::Result::invalidPath);
}

TEST_F(FatRenameTest, rename_beyond_the_path_slots_is_busy)
{
    for (int call = 0; call != 3; ++call)
        volume.Rename("/missing", "/other", [this](fs::Result result)
            {
                done.callback(result);
            });

    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::notFound)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatRenameTest, rename_waiting_behind_a_removal_reports_the_removal)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    volume.Rename("/notes.txt", "/memo.txt", [this](fs::Result result)
        {
            done.callback(result);
        });

    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    EXPECT_CALL(done, callback(fs::Result::mediaRemoved));
    ExecuteAllActions();
}

TEST_F(FatRenameWithSmallRootTest, full_fixed_root_is_no_space)
{
    for (uint32_t index = 0; index != 12; ++index)
    {
        char name[12] = "F0000000TXT";
        name[6] = static_cast<char>('0' + index / 10);
        name[7] = static_cast<char>('0' + index % 10);
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }

    ExpectRename("/Documents/REPORT.TXT", "/REPORT.TXT", fs::Result::noSpace);

    ExpectStat("/Documents/REPORT.TXT", fs::Result::success, fs::EntryInfo{ "REPORT.TXT", fs::EntryType::file, 700 });
}

TEST_F(FatRenameOnFat32Test, directory_moved_into_the_root_of_fat32_points_its_dot_dot_at_zero)
{
    ExpectRename("/Documents/SUB", "/SUB");

    EXPECT_EQ(0u, ParentOf(sub));
    ExpectStat("/SUB", fs::Result::success, fs::EntryInfo{ "SUB", fs::EntryType::directory, 0 });
}
