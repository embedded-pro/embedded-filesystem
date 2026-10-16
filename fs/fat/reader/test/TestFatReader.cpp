#include "fs/fat/reader/test/FatReaderFixture.hpp"

namespace
{
    class FatReaderMountTest
        : public FatReaderEveryTypeTest
    {};

    class FatReaderLookupTest
        : public FatReaderEveryTypeTest
    {};

    void ExpectEveryCallCompletesWith(fs::FatReader& volume, infra::ClockFixture& clock, fs::Result result)
    {
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
        volume.Remove("/a", [&done](fs::Result result)
            {
                done.callback(result);
            });
        volume.Rename("/a", "/b", [&done](fs::Result result)
            {
                done.callback(result);
            });
        volume.MakeDirectory("/a", [&done](fs::Result result)
            {
                done.callback(result);
            });
        volume.Usage([&done](fs::Result result, fs::VolumeUsage usage)
            {
                EXPECT_EQ(fs::VolumeUsage{}, usage);
                done.callback(result);
            });
        EXPECT_CALL(done, callback(result)).Times(4);
        clock.ExecuteAllActions();

        volume.OpenDirectory("/", [&done](fs::Result result, infra::SharedPtr<fs::Directory> directory)
            {
                EXPECT_EQ(nullptr, directory);
                done.callback(result);
            });
        volume.Open("/a", fs::OpenMode::read, [&done](fs::Result result, infra::SharedPtr<fs::File> file)
            {
                EXPECT_EQ(nullptr, file);
                done.callback(result);
            });
        volume.Open("/a", fs::OpenMode::write, [&done](fs::Result result, infra::SharedPtr<fs::File> file)
            {
                EXPECT_EQ(nullptr, file);
                done.callback(result);
            });
        volume.Stat("/", [&done](fs::Result result, const fs::EntryInfo& info)
            {
                EXPECT_EQ(fs::EntryInfo{}, info);
                done.callback(result);
            });
        EXPECT_CALL(done, callback(result)).Times(4);
        clock.ExecuteAllActions();
    }
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatReaderMountTest, testing::Values(fat12Volume, fat16Volume, fat16WithoutPartitionTable, fat32Volume));
INSTANTIATE_TEST_SUITE_P(EveryType, FatReaderLookupTest, testing::Values(fat12Volume, fat16Volume, fat32Volume));

TEST_P(FatReaderMountTest, mounts_by_reading_only_the_boot_sector_and_the_partition_table)
{
    auto reads = device.ReadRequests();
    auto writes = device.WriteRequests();

    ExpectMount();

    EXPECT_EQ(GetParam().partitioned ? 2u : 1u, device.ReadRequests() - reads);
    EXPECT_EQ(writes, device.WriteRequests());
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatReaderTest, calls_before_mount_are_not_mounted)
{
    ExpectEveryCallCompletesWith(volume, *this, fs::Result::notMounted);
    ExpectUnmount(fs::Result::notMounted);
}

TEST_F(FatReaderTest, mount_completes_from_the_event_dispatcher)
{
    StartMount();
    testing::Mock::VerifyAndClearExpectations(&mountDone);

    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatReaderTest, unmount_from_the_mount_completion_completes_once)
{
    volume.Mount([this](fs::Result result)
        {
            mountDone.callback(result);
            StartUnmount();
        });

    EXPECT_CALL(mountDone, callback(fs::Result::success));
    EXPECT_CALL(unmountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatReaderTest, mount_from_the_failed_mount_completion_completes_once)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);
    volume.Mount([this](fs::Result result)
        {
            mountDone.callback(result);
            StartMount();
        });

    EXPECT_CALL(mountDone, callback(fs::Result::deviceError));
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatReaderTest, unformatted_device_is_corrupted)
{
    WriteSector(0, Sector{});

    ExpectMount(fs::Result::corrupted);
    ExpectStat("/", fs::Result::notMounted);
}

TEST_F(FatReaderTest, protective_gpt_is_unsupported)
{
    Sector mbr{};
    mbr[446 + 4] = 0xee;
    mbr[446 + 12] = 0xff;
    mbr[510] = 0x55;
    mbr[511] = 0xaa;
    WriteSector(0, mbr);

    ExpectMount(fs::Result::unsupported);
}

TEST_F(FatReaderTest, partition_beyond_the_device_is_corrupted)
{
    auto mbr = ReadSector(0);
    mbr[446 + 14] = 0x10;
    WriteSector(0, mbr);

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatReaderTest, partition_starting_beyond_the_device_is_corrupted)
{
    auto mbr = ReadSector(0);
    mbr[446 + 10] = 0x10;
    WriteSector(0, mbr);

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatReaderTest, invalid_boot_sector_in_a_partition_is_corrupted)
{
    WriteSector(PartitionStart(), Sector{});

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatReaderTest, inconsistent_boot_sector_reports_its_geometry_result)
{
    auto boot = ReadSector(PartitionStart());
    boot[0x20] = 0xff;
    boot[0x21] = 0xff;
    boot[0x22] = 0xff;
    boot[0x13] = 0;
    boot[0x14] = 0;
    WriteSector(PartitionStart(), boot);

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatReaderTest, block_size_other_than_512_is_unsupported)
{
    fs::SparseBlockDevice::WithStorage<1, 4096> largeBlocks{ 1024 };
    fs::FatReader::WithStorage<1> largeVolume{ largeBlocks };

    largeVolume.Mount([this](fs::Result result)
        {
            mountDone.callback(result);
        });
    EXPECT_CALL(mountDone, callback(fs::Result::unsupported));
    ExecuteAllActions();

    EXPECT_EQ(0u, largeBlocks.ReadRequests());
}

TEST_F(FatReaderTest, device_error_faults_and_a_new_mount_succeeds)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);
    ExpectMount(fs::Result::deviceError);
    ExpectStat("/", fs::Result::notMounted);

    ExpectMount();
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatReaderTest, removed_card_is_reported_until_the_next_mount)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    ExpectMount(fs::Result::mediaRemoved);

    ExpectEveryCallCompletesWith(volume, *this, fs::Result::mediaRemoved);
    ExpectUnmount(fs::Result::mediaRemoved);

    ExpectMount();
}

TEST_F(FatReaderTest, handles_from_before_a_removal_do_not_fault_the_next_mount)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "LOG     TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/log.txt");
    auto directory = ExpectOpenDirectory("/");

    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::mediaRemoved, 0);
    ExpectMount();

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::mediaRemoved, 0);
    ExpectNext(*directory, fs::Result::mediaRemoved);
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatReaderTest, failure_reading_the_boot_sector_of_a_partition_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 1);

    ExpectMount(fs::Result::deviceError);
}

TEST_F(FatReaderTest, mount_while_mounting_or_mounted_is_busy)
{
    StartMount();
    StartMount();

    EXPECT_CALL(mountDone, callback(fs::Result::busy));
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectMount(fs::Result::busy);
}

TEST_F(FatReaderTest, unmount_while_mounting_or_unmounting_is_busy)
{
    StartMount();
    StartUnmount();

    EXPECT_CALL(unmountDone, callback(fs::Result::busy));
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();

    StartUnmount();
    StartUnmount();
    EXPECT_CALL(unmountDone, callback(fs::Result::busy));
    EXPECT_CALL(unmountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatReaderTest, unmounted_volume_is_not_mounted_and_mounts_again)
{
    ExpectMount();

    ExpectUnmount();
    ExpectEveryCallCompletesWith(volume, *this, fs::Result::notMounted);
    ExpectUnmount(fs::Result::notMounted);

    ExpectMount();
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatReaderTest, unmount_does_not_access_the_device)
{
    ExpectMount();
    auto reads = device.ReadRequests();
    auto writes = device.WriteRequests();
    auto flushes = device.FlushRequests();

    ExpectUnmount();

    EXPECT_EQ(reads, device.ReadRequests());
    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(flushes, device.FlushRequests());
}

TEST_F(FatReaderTest, mount_after_unmount_reads_the_device_again)
{
    ExpectMount();
    ExpectUnmount();
    auto reads = device.ReadRequests();

    ExpectMount();

    EXPECT_EQ(reads + 2, device.ReadRequests());
}

TEST_F(FatReaderTest, writing_calls_are_read_only_and_usage_is_unsupported)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();

    ExpectOpen("/file.txt", fs::Result::readOnly, fs::OpenMode::write);
    ExpectOpen("/file.txt", fs::Result::readOnly, fs::OpenMode::append);
    ExpectOpen("/new.txt", fs::Result::readOnly, fs::OpenMode::readWrite);
    volume.Remove("/file.txt", Done());
    ExpectDone(fs::Result::readOnly);
    volume.Rename("/file.txt", "/other.txt", Done());
    ExpectDone(fs::Result::readOnly);
    volume.MakeDirectory("/new", Done());
    ExpectDone(fs::Result::readOnly);
    volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
        {
            EXPECT_EQ(fs::VolumeUsage{}, usage);
            done.callback(result);
        });
    ExpectDone(fs::Result::unsupported);
}

TEST_P(FatReaderLookupTest, stat_finds_an_entry_by_its_long_and_its_short_name)
{
    auto contents = Pattern<700>();
    builder->AddFile(0, "Meeting notes.txt", "MEETIN~1TXT", infra::MakeRange(contents));
    ExpectMount();

    ExpectStat("/Meeting notes.txt", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/meeting NOTES.TXT", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/MEETIN~1.TXT", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/missing.txt", fs::Result::notFound);
}

TEST_P(FatReaderLookupTest, nested_directories_are_walked)
{
    auto contents = Pattern<100>();
    auto documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
    auto reports = builder->AddDirectory(documents, "", "REPORTS    ");
    builder->AddFile(reports, "Quarterly report.pdf", "QUARTE~1PDF", infra::MakeRange(contents));
    ExpectMount();

    ExpectStat("/Documents/reports/Quarterly report.pdf", fs::Result::success, fs::EntryInfo{ "Quarterly report.pdf", fs::EntryType::file, 100 });
    ExpectStat("/DOCUME~1/REPORTS", fs::Result::success, fs::EntryInfo{ "REPORTS", fs::EntryType::directory, 0 });
    ExpectStat("/Documents/missing/Quarterly report.pdf", fs::Result::notFound);
    ExpectStat("/Documents/reports/Quarterly report.pdf/x", fs::Result::notADirectory);
}

TEST_F(FatReaderTest, paths_are_validated_against_the_limits)
{
    ExpectMount();

    ExpectStat("relative", fs::Result::invalidPath);
    ExpectStat("/a/", fs::Result::invalidPath);
    ExpectStat("/a/../b", fs::Result::invalidPath);
    ExpectStat("/a*b", fs::Result::invalidPath);
    ExpectStat("/name ending with a dot.", fs::Result::invalidPath);

    std::array<char, 257> tooLong;
    tooLong.fill('a');
    tooLong[0] = '/';
    ExpectStat(infra::BoundedConstString(tooLong.data(), tooLong.size()), fs::Result::nameTooLong);
}

TEST_F(FatReaderTest, limits_size_the_names_and_paths)
{
    fs::FatReader::WithStorage<1, 1, fs::PathLimits{ 40, 64 }> limited{ device };
    builder->AddFile(0, "A name that is longer than forty bytes.txt", "ANAMET~1TXT", infra::ConstByteRange());
    limited.Mount(Done());
    ExpectDone(fs::Result::success);

    std::array<char, 42> longName;
    longName.fill('a');
    longName[0] = '/';
    std::array<char, 65> longPath;
    longPath.fill('a');
    longPath[0] = '/';
    longPath[32] = '/';

    for (auto path : { infra::BoundedConstString(longName.data(), longName.size()), infra::BoundedConstString(longPath.data(), longPath.size()), infra::BoundedConstString("/ANAMET~1.TXT") })
        limited.Stat(path, [this](fs::Result result, const fs::EntryInfo& info)
            {
                statDone.callback(result, info);
            });

    EXPECT_CALL(statDone, callback(fs::Result::nameTooLong, fs::EntryInfo{})).Times(2);
    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryInfo{ "ANAMET~1.TXT", fs::EntryType::file, 0 }));
    ExecuteAllActions();
}

TEST_F(FatReaderTest, path_calls_while_one_runs_are_busy)
{
    ExpectMount();
    StartStat("/");
    volume.Open("/a", fs::OpenMode::read, [this](fs::Result result, infra::SharedPtr<fs::File> file)
        {
            EXPECT_EQ(nullptr, file);
            openDone.callback(result);
        });
    volume.OpenDirectory("/", [this](fs::Result result, infra::SharedPtr<fs::Directory> directory)
        {
            EXPECT_EQ(nullptr, directory);
            openDone.callback(result);
        });

    EXPECT_CALL(openDone, callback(fs::Result::busy)).Times(2);
    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 }));
    ExecuteAllActions();

    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatReaderTest, a_path_call_from_the_completion_of_another_runs)
{
    ExpectMount();
    volume.Stat("/", [this](fs::Result result, const fs::EntryInfo& info)
        {
            statDone.callback(result, info);
            StartStat("/");
        });

    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 })).Times(2);
    ExecuteAllActions();
}

TEST_F(FatReaderTest, open_and_open_directory_check_the_kind_of_entry)
{
    builder->AddDirectory(0, "", "SUB        ");
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();

    ExpectOpen("/", fs::Result::isADirectory);
    ExpectOpen("/sub", fs::Result::isADirectory);
    ExpectOpen("/missing", fs::Result::notFound);
    ExpectOpen("/sub/missing", fs::Result::notFound);
    ExpectOpen("/file.txt/missing", fs::Result::notADirectory);
    ExpectOpenDirectory("/file.txt", fs::Result::notADirectory);
    ExpectOpenDirectory("/missing", fs::Result::notFound);
    ExpectOpenDirectory("/sub");
    ExpectOpen("/file.txt");
}

TEST_F(FatReaderTest, entries_without_a_valid_first_cluster_are_corrupted)
{
    fs::ShortEntry file;
    file.name.bytes = { 'F', 'I', 'L', 'E', ' ', ' ', ' ', ' ', 'T', 'X', 'T' };
    file.size = 10;
    builder->AddEntries(0, "", file);
    fs::ShortEntry directory;
    directory.name.bytes = { 'S', 'U', 'B', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    directory.attributes = fs::FatAttributes::directory;
    builder->AddEntries(0, "", directory);
    ExpectMount();

    ExpectOpen("/file.txt", fs::Result::corrupted);
    ExpectOpenDirectory("/sub", fs::Result::corrupted);
    ExpectStat("/sub/x", fs::Result::corrupted);
}

TEST_F(FatReaderTest, handles_beyond_the_pools_are_too_many)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();
    auto first = ExpectOpen("/file.txt");
    auto second = ExpectOpen("/file.txt");
    auto firstDirectory = ExpectOpenDirectory("/");
    auto secondDirectory = ExpectOpenDirectory("/");

    ExpectOpen("/file.txt", fs::Result::tooManyOpenFiles);
    ExpectOpenDirectory("/", fs::Result::tooManyOpenFiles);

    first = nullptr;
    firstDirectory = nullptr;
    ExpectOpen("/file.txt");
    ExpectOpenDirectory("/");
}

TEST_F(FatReaderTest, call_made_before_unmount_completes_normally)
{
    ExpectMount();
    StartStat("/");
    StartUnmount();

    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 }));
    EXPECT_CALL(unmountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatReaderTest, call_made_while_unmounting_is_not_mounted)
{
    ExpectMount();
    StartUnmount();
    StartStat("/");

    EXPECT_CALL(statDone, callback(fs::Result::notMounted, fs::EntryInfo{}));
    EXPECT_CALL(unmountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatReaderTest, device_error_during_a_lookup_is_reported_and_the_volume_stays_mounted)
{
    ExpectMount();
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectStat("/a", fs::Result::deviceError);
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatReaderTest, removed_card_during_a_lookup_faults_the_volume)
{
    ExpectMount();
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ExpectStat("/a", fs::Result::mediaRemoved);
    ExpectEveryCallCompletesWith(volume, *this, fs::Result::mediaRemoved);

    ExpectMount();
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(FatReaderTest, path_call_waiting_behind_a_read_that_finds_the_card_removed_is_media_removed)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    StartRead(*file, infra::MakeRange(buffer));
    StartStat("/file.txt");

    EXPECT_CALL(readDone, callback(fs::Result::mediaRemoved, 0));
    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    ExecuteAllActions();
}
