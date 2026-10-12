#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include "infra/util/BoundedVector.hpp"
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

    fs::FatConfig WithoutDiscard()
    {
        auto config = LongPaths();
        config.discard = false;
        return config;
    }

    class FatRemoveTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatRemoveTest(const fs::FatConfig& config = LongPaths())
            : FatFileSystemFixture(16384, ClusterSize(1), config)
        {}

        void SetUp() override
        {
            notesCluster = builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(notes));
            builder->AddFile(0, "", "EMPTY   TXT", infra::ConstByteRange());
            documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
            builder->AddFile(documents, "", "REPORT  TXT", infra::MakeRange(notes));
            empty = builder->AddDirectory(0, "", "EMPTY      ");
            ExpectMount();
        }

        void ExpectRemove(infra::BoundedConstString path, fs::Result result = fs::Result::success)
        {
            volume.Remove(path, [this](fs::Result result)
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

        uint8_t FirstByteAt(fs::FatEntryPosition position) const
        {
            return builder->ReadSector(builder->SectorOf(position))[position.index % 16 * 32];
        }

        std::array<uint8_t, 700> notes{};
        uint32_t notesCluster{ 0 };
        uint32_t documents{ 0 };
        uint32_t empty{ 0 };
        infra::BoundedVector<std::pair<fs::SparseBlockDevice::Request, uint32_t>>::WithMaxSize<32> requests;
    };

    class FatRemoveWithoutDiscardTest
        : public FatRemoveTest
    {
    public:
        FatRemoveWithoutDiscardTest()
            : FatRemoveTest(WithoutDiscard())
        {}
    };

    using Request = fs::SparseBlockDevice::Request;
}

TEST_F(FatRemoveTest, removed_file_is_gone_and_its_clusters_are_free)
{
    ExpectRemove("/NOTES.TXT");
    ExpectUnmount();

    EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ 0, 0 }));
    EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ 0, 1 }));
    EXPECT_EQ(0u, builder->FatEntry(notesCluster));
    EXPECT_EQ(0u, builder->FatEntry(notesCluster + 1));
    ExpectMount();
    ExpectStat("/notes.txt", fs::Result::notFound);
}

TEST_F(FatRemoveTest, entry_is_cleared_and_flushed_before_the_clusters_are_freed_discarded_and_flushed)
{
    RecordRequests();

    ExpectRemove("/notes.txt");

    const auto& geometry = builder->Geometry();
    ASSERT_EQ(6u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ(std::make_pair(Request::erase, geometry.SectorOfCluster(notesCluster)), requests[2]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector), requests[3]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector + geometry.sectorsPerFat), requests[4]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[5]);
    EXPECT_EQ(0u, builder->FatEntry(notesCluster));
}

TEST_F(FatRemoveTest, contiguous_chain_is_discarded_in_one_request)
{
    auto erases = device.EraseRequests();

    ExpectRemove("/notes.txt");

    EXPECT_EQ(erases + 1, device.EraseRequests());
}

TEST_F(FatRemoveTest, fragmented_chain_is_discarded_one_run_at_a_time)
{
    std::array<uint8_t, 1500> contents{};
    auto first = builder->AddFile(0, "", "SPREAD  BIN", infra::MakeRange(contents), true);
    auto erases = device.EraseRequests();

    ExpectRemove("/spread.bin");
    ExpectUnmount();

    EXPECT_EQ(erases + 3, device.EraseRequests());
    EXPECT_EQ(0u, builder->FatEntry(first));
}

TEST_F(FatRemoveWithoutDiscardTest, clusters_are_not_discarded_when_discard_is_disabled)
{
    auto erases = device.EraseRequests();

    ExpectRemove("/notes.txt");

    EXPECT_EQ(erases, device.EraseRequests());
}

TEST_F(FatRemoveTest, empty_file_frees_nothing)
{
    auto erases = device.EraseRequests();

    ExpectRemove("/empty.txt");

    EXPECT_EQ(erases, device.EraseRequests());
    ExpectStat("/empty.txt", fs::Result::notFound);
}

TEST_F(FatRemoveTest, removed_space_is_reported_free)
{
    fs::VolumeUsage before;
    volume.Usage([&before](fs::Result, fs::VolumeUsage usage)
        {
            before = usage;
        });
    ExecuteAllActions();

    ExpectRemove("/notes.txt");

    fs::VolumeUsage after;
    volume.Usage([&after](fs::Result, fs::VolumeUsage usage)
        {
            after = usage;
        });
    ExecuteAllActions();
    EXPECT_EQ(before.usedBytes - 2 * 512, after.usedBytes);
}

TEST_F(FatRemoveTest, empty_directory_is_removed)
{
    ExpectRemove("/empty");
    ExpectUnmount();

    EXPECT_EQ(0u, builder->FatEntry(empty));
    ExpectMount();
    ExpectStat("/empty", fs::Result::notFound);
}

TEST_F(FatRemoveTest, directory_with_entries_is_not_empty)
{
    ExpectRemove("/documents", fs::Result::notEmpty);

    ExpectStat("/documents", fs::Result::success, fs::EntryInfo{ "Documents", fs::EntryType::directory, 0 });
}

TEST_F(FatRemoveTest, directory_whose_entries_were_removed_is_empty)
{
    ExpectRemove("/documents/report.txt");

    ExpectRemove("/documents");
}

TEST_F(FatRemoveTest, root_and_missing_entries_cannot_be_removed)
{
    ExpectRemove("/", fs::Result::invalidPath);
    ExpectRemove("/missing.txt", fs::Result::notFound);
    ExpectRemove("/missing/notes.txt", fs::Result::notFound);
    ExpectRemove("/notes.txt/inner", fs::Result::notADirectory);
}

TEST_F(FatRemoveTest, read_only_file_cannot_be_removed)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'L', 'O', 'C', 'K', 'E', 'D', ' ', ' ', 'T', 'X', 'T' };
    entry.attributes = fs::FatAttributes::readOnly;
    builder->AddEntries(0, "", entry);

    ExpectRemove("/locked.txt", fs::Result::readOnly);
}

TEST_F(FatRemoveTest, entry_with_a_cluster_outside_the_volume_is_corrupted)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'B', 'A', 'D', ' ', ' ', ' ', ' ', ' ', 'T', 'X', 'T' };
    entry.firstCluster = builder->Geometry().clusterCount + 2;
    entry.size = 10;
    builder->AddEntries(0, "", entry);

    ExpectRemove("/bad.txt", fs::Result::corrupted);
}

TEST_F(FatRemoveTest, open_file_is_busy)
{
    auto file = ExpectOpen("/notes.txt");

    ExpectRemove("/notes.txt", fs::Result::busy);

    file = nullptr;
    ExecuteAllActions();
    ExpectRemove("/notes.txt");
}

TEST_F(FatRemoveTest, open_directory_is_busy)
{
    auto directory = ExpectOpenDirectory("/empty");

    ExpectRemove("/empty", fs::Result::busy);

    directory = nullptr;
    ExecuteAllActions();
    ExpectRemove("/empty");
}

TEST_F(FatRemoveTest, directory_handle_from_before_an_unmount_does_not_make_a_directory_busy)
{
    auto directory = ExpectOpenDirectory("/empty");
    ExpectUnmount();
    ExpectMount();

    ExpectRemove("/empty");
}

TEST_F(FatRemoveTest, set_spanning_two_sectors_clears_its_short_entry_first)
{
    for (uint32_t index = 0; index != 8; ++index)
    {
        char name[12] = "FILL0000TXT";
        name[7] = static_cast<char>('0' + index);
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }
    auto position = builder->AddEntries(0, "A Long Name.txt", fs::ShortEntry{ fs::ShortName{ { 'A', 'L', 'O', 'N', 'G', 'N', '~', '1', 'T', 'X', 'T' } } });
    ASSERT_EQ((fs::FatEntryPosition{ 0, 16 }), position);
    RecordRequests();

    ExpectRemove("/a long name.txt");

    ASSERT_EQ(4u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, builder->Geometry().rootSector + 1), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ(std::make_pair(Request::write, builder->Geometry().rootSector), requests[2]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[3]);
    EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ 0, 14 }));
    EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ 0, 15 }));
    EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ 0, 16 }));
}

TEST_F(FatRemoveTest, longest_name_across_three_clusters_is_cleared)
{
    std::array<char, 255> name;
    name.fill('n');
    std::array<char, 1 + 9 + 1 + 255> path{};
    std::copy_n("/documents/", 11, path.begin());
    std::copy(name.begin(), name.end(), path.begin() + 11);
    builder->AddFile(documents, "", "FILL0000TXT", infra::ConstByteRange());
    for (uint32_t index = 1; index != 12; ++index)
    {
        char filler[12] = "FILL0000TXT";
        filler[6] = static_cast<char>('0' + index / 10);
        filler[7] = static_cast<char>('0' + index % 10);
        builder->AddFile(documents, "", filler, infra::ConstByteRange());
    }
    infra::BoundedConstString pathView(path.data(), path.size());
    ExpectOpen(pathView, fs::OpenMode::write);
    ExecuteAllActions();

    ExpectRemove(pathView);
    ExpectUnmount();

    auto second = builder->FatEntry(documents);
    auto third = builder->FatEntry(second);
    EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ documents, 15 }));
    for (uint32_t index = 0; index != 16; ++index)
        EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ second, index }));
    for (uint32_t index = 0; index != 4; ++index)
        EXPECT_EQ(0xe5, FirstByteAt(fs::FatEntryPosition{ third, index }));
}

TEST_F(FatRemoveTest, chain_that_reaches_a_free_cluster_is_corrupted)
{
    builder->SetFatEntry(notesCluster + 1, 0);

    ExpectRemove("/notes.txt", fs::Result::corrupted);
}

TEST_F(FatRemoveTest, failed_flush_after_freeing_the_chain_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 6);

    ExpectRemove("/notes.txt", fs::Result::deviceError);
}

TEST_F(FatRemoveTest, failed_flush_after_clearing_the_entry_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 2);

    ExpectRemove("/notes.txt", fs::Result::deviceError);
    EXPECT_EQ(notesCluster + 1, builder->FatEntry(notesCluster));
}

TEST_F(FatRemoveTest, device_error_while_checking_that_a_directory_is_empty_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 1);

    ExpectRemove("/empty", fs::Result::deviceError);
}

TEST_F(FatRemoveTest, failure_to_clear_the_entry_is_reported)
{
    for (uint32_t index = 0; index != 8; ++index)
    {
        char name[12] = "FILL0000TXT";
        name[7] = static_cast<char>('0' + index);
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }
    ExpectOpen("/A Long Name.txt", fs::OpenMode::write);
    ExecuteAllActions();
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ExpectRemove("/a long name.txt", fs::Result::deviceError);
}

TEST_F(FatRemoveTest, invalid_path_is_rejected_before_it_is_queued)
{
    ExpectRemove("relative", fs::Result::invalidPath);
}

TEST_F(FatRemoveTest, remove_beyond_the_path_slots_is_busy)
{
    for (int call = 0; call != 3; ++call)
        volume.Remove("/missing", [this](fs::Result result)
            {
                done.callback(result);
            });

    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::notFound)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatRemoveTest, remove_waiting_behind_a_removal_reports_the_removal)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    volume.Remove("/notes.txt", [this](fs::Result result)
        {
            done.callback(result);
        });

    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    EXPECT_CALL(done, callback(fs::Result::mediaRemoved));
    ExecuteAllActions();
}

namespace
{
    class ExposedFileSystem
        : public fs::FatFileSystem::WithStorage<2, 4>
    {
    public:
        using fs::FatFileSystem::WithStorage<2, 4>::WithStorage;
        using fs::FatFileSystem::Volume;
    };

    class FatRemoveMemoTest
        : public FatFileSystemFixture
    {
    public:
        void SetUp() override
        {
            builder->AddFile(0, "", "NOTES   TXT", infra::MakeRange(notes));
            exposed.Mount([this](fs::Result result)
                {
                    done.callback(result);
                });
            ExpectDone(fs::Result::success);
        }

        std::array<uint8_t, 700> notes{};
        ExposedFileSystem exposed{ device };
    };
}

TEST_F(FatRemoveMemoTest, removed_file_is_forgotten_by_the_memo_of_closed_files)
{
    infra::SharedPtr<fs::File> file;
    exposed.Open("/notes.txt", fs::OpenMode::read, [&file](fs::Result, infra::SharedPtr<fs::File> opened)
        {
            file = opened;
        });
    ExecuteAllActions();
    auto firstCluster = static_cast<fs::FatFile&>(*file).State().firstCluster;
    file = nullptr;
    ExecuteAllActions();
    ASSERT_NE(std::nullopt, exposed.Volume().ClosedFiles().Recall(fs::FatEntryPosition{ 0, 0 }, firstCluster, 700));

    exposed.Remove("/notes.txt", [this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::success);

    EXPECT_EQ(std::nullopt, exposed.Volume().ClosedFiles().Recall(fs::FatEntryPosition{ 0, 0 }, firstCluster, 700));
}
