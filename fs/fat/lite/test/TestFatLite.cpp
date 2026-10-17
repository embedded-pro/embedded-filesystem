#include "fs/fat/FatTable.hpp"
#include "fs/fat/lite/test/FatLiteFixture.hpp"

namespace
{
    class FatLiteWithSmallRootTest
        : public testing::Test
        , public FatLiteFixture
    {
    public:
        FatLiteWithSmallRootTest()
            : FatLiteFixture(fat16WithSmallRoot)
        {}
    };

    class FatLiteOnFat32Test
        : public testing::Test
        , public FatLiteFixture
    {
    public:
        FatLiteOnFat32Test()
            : FatLiteFixture(fat32Volume)
        {}
    };

    class FatLiteOnFat12Test
        : public testing::Test
        , public FatLiteFixture
    {
    public:
        FatLiteOnFat12Test()
            : FatLiteFixture(fat12Volume)
        {}
    };
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatLiteEveryTypeTest, testing::Values(fat12Volume, fat16Volume, fat32Volume));

TEST_P(FatLiteEveryTypeTest, a_written_file_reads_back_here_and_on_the_full_volume)
{
    auto contents = Pattern<5000>();
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(contents));
    ExpectContents("/log.txt", contents);
    ExpectUnmount();

    ExpectContentsOnFullVolume("/log.txt", contents);
}

TEST_P(FatLiteEveryTypeTest, files_in_a_directory_written_by_the_full_volume_are_written_and_read)
{
    {
        fs::FatFileSystem::WithStorage<1, 4> full{ device };
        full.Mount(Done());
        ExpectDone(fs::Result::success);
        full.MakeDirectory("/logs", Done());
        ExpectDone(fs::Result::success);
        full.Unmount(Done());
        ExpectDone(fs::Result::success);
    }

    auto contents = Pattern<1200>(7);
    ExpectMount();
    WriteFile("/logs/day1.txt", infra::MakeRange(contents));
    ExpectUnmount();

    ExpectContentsOnFullVolume("/logs/day1.txt", contents);
}

TEST_F(FatLiteTest, calls_before_mount_are_not_mounted)
{
    volume.Open("/log.txt", fs::OpenMode::write, [this](fs::Result result, infra::SharedPtr<fs::File>)
        {
            done.callback(result);
        });
    volume.MakeDirectory("/logs", Done());
    volume.Remove("/log.txt", Done());
    volume.Rename("/a", "/b", Done());
    volume.Usage([this](fs::Result result, fs::VolumeUsage)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::notMounted, 5);

    volume.Unmount(Done());
    ExpectDone(fs::Result::notMounted);
}

TEST_F(FatLiteTest, is_idle_unless_a_call_waits_or_runs)
{
    EXPECT_TRUE(volume.Idle());

    volume.Mount(Done());
    EXPECT_FALSE(volume.Idle());
    ExecuteFirstAction();
    EXPECT_FALSE(volume.Idle());

    ExpectDone(fs::Result::success);
    EXPECT_TRUE(volume.Idle());
}

TEST_F(FatLiteTest, calls_it_leaves_out_are_unsupported_before_their_paths_are_validated)
{
    ExpectMount();

    volume.MakeDirectory("relative", Done());
    volume.Remove("/log.txt", Done());
    volume.Rename("/a", "/b/", Done());
    volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
        {
            EXPECT_EQ(fs::VolumeUsage{}, usage);
            done.callback(result);
        });
    ExpectDone(fs::Result::unsupported, 4);
}

TEST_F(FatLiteTest, open_in_write_mode_creates_an_empty_file_with_its_case)
{
    ExpectMount();
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::write);
    EXPECT_EQ(0u, file->Size());
    ExpectClose(*file);

    ExpectStat("/NOTES.TXT", fs::Result::success, fs::EntryInfo{ "notes.txt", fs::EntryType::file, 0 });
}

TEST_F(FatLiteTest, every_writable_mode_creates_and_read_mode_does_not)
{
    ExpectMount();
    ExpectOpen("/a.log", fs::OpenMode::append);
    ExpectOpen("/b.log", fs::OpenMode::readWrite);
    ExpectOpen("/c.log", fs::OpenMode::read, fs::Result::notFound);

    ExpectStat("/a.log", fs::Result::success, fs::EntryInfo{ "a.log", fs::EntryType::file, 0 });
    ExpectStat("/b.log", fs::Result::success, fs::EntryInfo{ "b.log", fs::EntryType::file, 0 });
    ExpectStat("/c.log", fs::Result::notFound);
}

TEST_F(FatLiteTest, names_that_need_long_name_entries_are_unsupported)
{
    ExpectMount();

    ExpectOpen("/Meeting notes.txt", fs::OpenMode::write, fs::Result::unsupported);
    ExpectOpen("/Notes.txt", fs::OpenMode::write, fs::Result::unsupported);
    ExpectOpen("/a+b.txt", fs::OpenMode::write, fs::Result::unsupported);
}

TEST_F(FatLiteTest, without_long_names_files_are_found_by_their_short_names_and_others_keep_their_long_names)
{
    auto contents = Pattern<100>();
    auto created = Pattern<20>(1);
    builder->AddFile(0, "Meeting notes.txt", "MEETIN~1TXT", infra::MakeRange(contents));

    {
        fs::FatLite::WithStorage<1, 1, fs::PathLimits{}, fs::FatReaderFeatures{ .longNames = false }> shortNames{ device };
        shortNames.Mount(Done());
        ExpectDone(fs::Result::success);

        ExpectStat(shortNames, "/MEETIN~1.TXT", fs::Result::success, fs::EntryInfo{ "MEETIN~1.TXT", fs::EntryType::file, 100 });
        ExpectStat(shortNames, "/Meeting notes.txt", fs::Result::notFound);
        auto file = ExpectOpen(shortNames, "/new.txt", fs::OpenMode::write);
        ExpectWrite(*file, infra::MakeRange(created));
        ExpectClose(*file);
        file = nullptr;

        shortNames.Unmount(Done());
        ExpectDone(fs::Result::success);
    }

    ExpectContentsOnFullVolume("/Meeting notes.txt", contents);
    ExpectContentsOnFullVolume("/new.txt", created);
}

TEST_F(FatLiteTest, invalid_names_and_paths_are_refused)
{
    ExpectMount();

    ExpectOpen("relative", fs::OpenMode::write, fs::Result::invalidPath);
    ExpectOpen("/a?.txt", fs::OpenMode::write, fs::Result::invalidPath);
    ExpectOpen("/missing/a.txt", fs::OpenMode::write, fs::Result::notFound);
    ExpectOpen("/", fs::OpenMode::write, fs::Result::isADirectory);
}

TEST_F(FatLiteTest, a_file_inside_a_file_is_not_a_directory)
{
    ExpectMount();
    ExpectOpen("/a.txt", fs::OpenMode::write);

    ExpectOpen("/a.txt/b.txt", fs::OpenMode::write, fs::Result::notADirectory);
}

TEST_F(FatLiteTest, a_directory_does_not_open_as_a_file)
{
    builder->AddDirectory(0, "", "LOGS       ");
    ExpectMount();

    ExpectOpen("/logs", fs::OpenMode::read, fs::Result::isADirectory);
    ExpectOpen("/logs", fs::OpenMode::write, fs::Result::isADirectory);
}

TEST_F(FatLiteTest, a_read_only_file_opens_for_reading_only)
{
    auto contents = Pattern<100>();
    fs::ShortEntry entry;
    entry.name = fs::ShortName{ { 'R', 'O', ' ', ' ', ' ', ' ', ' ', ' ', 'T', 'X', 'T' } };
    entry.attributes = fs::FatAttributes::readOnly;
    builder->AddEntries(0, "", entry);
    ExpectMount();

    ExpectOpen("/ro.txt", fs::OpenMode::read);
    ExpectOpen("/ro.txt", fs::OpenMode::write, fs::Result::readOnly);
    ExpectOpen("/ro.txt", fs::OpenMode::append, fs::Result::readOnly);
}

TEST_F(FatLiteTest, a_file_opens_many_times_for_reading_and_once_otherwise)
{
    ExpectMount();
    auto writer = ExpectOpen("/log.txt", fs::OpenMode::write);
    ExpectOpen("/log.txt", fs::OpenMode::read, fs::Result::busy);
    ExpectOpen("/log.txt", fs::OpenMode::append, fs::Result::busy);
    ExpectClose(*writer);
    writer = nullptr;

    auto first = ExpectOpen("/log.txt", fs::OpenMode::read);
    auto second = ExpectOpen("/log.txt", fs::OpenMode::read);
    ExpectOpen("/log.txt", fs::OpenMode::readWrite, fs::Result::busy);
}

TEST_F(FatLiteTest, the_pool_of_files_runs_out)
{
    ExpectMount();
    auto first = ExpectOpen("/a.txt", fs::OpenMode::write);
    auto second = ExpectOpen("/b.txt", fs::OpenMode::write);

    ExpectOpen("/c.txt", fs::OpenMode::write, fs::Result::tooManyOpenFiles);
    ExpectStat("/c.txt", fs::Result::notFound);
}

TEST_F(FatLiteTest, a_second_path_call_while_one_is_pending_is_busy)
{
    ExpectMount();

    volume.Open("/a.txt", fs::OpenMode::write, [this](fs::Result result, infra::SharedPtr<fs::File>)
        {
            done.callback(result);
        });
    volume.Stat("/a.txt", [this](fs::Result result, const fs::EntryInfo&)
        {
            done.callback(result);
        });
    volume.OpenDirectory("/", [this](fs::Result result, infra::SharedPtr<fs::Directory>)
        {
            done.callback(result);
        });
    volume.Open("/b.txt", fs::OpenMode::write, [this](fs::Result result, infra::SharedPtr<fs::File>)
        {
            done.callback(result);
        });

    EXPECT_CALL(done, callback(fs::Result::busy)).Times(3);
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatLiteTest, created_files_are_listed)
{
    ExpectMount();
    ExpectOpen("/a.txt", fs::OpenMode::write);
    ExpectOpen("/b.txt", fs::OpenMode::write);

    infra::SharedPtr<fs::Directory> directory;
    volume.OpenDirectory("/", [this, &directory](fs::Result result, infra::SharedPtr<fs::Directory> opened)
        {
            directory = opened;
            done.callback(result);
        });
    ExpectDone(fs::Result::success);

    std::array<std::optional<fs::EntryInfo>, 3> listed;
    for (auto& entry : listed)
    {
        directory->Next([this, &entry](fs::Result result, std::optional<fs::EntryInfo> info)
            {
                if (info)
                    entry = fs::EntryInfo{ info->name, info->type, info->size };

                done.callback(result);
            });
        ExpectDone(fs::Result::success);
        if (entry)
            EXPECT_EQ(fs::EntryType::file, entry->type);
    }

    EXPECT_TRUE(listed[0].has_value());
    EXPECT_TRUE(listed[1].has_value());
    EXPECT_FALSE(listed[2].has_value());
}

TEST_F(FatLiteWithSmallRootTest, a_full_fixed_root_has_no_space)
{
    ExpectMount();

    for (char name = 'a'; name != 'a' + 16; ++name)
    {
        std::array<char, 7> path{ '/', name, '.', 't', 'x', 't', '\0' };
        auto file = ExpectOpen(infra::BoundedConstString(path.data(), 6), fs::OpenMode::write);
        ExpectClose(*file);
    }

    ExpectOpen("/z.txt", fs::OpenMode::write, fs::Result::noSpace);
}

TEST_F(FatLiteTest, a_full_directory_grows_by_a_cluster_of_zeros)
{
    auto logs = builder->AddDirectory(0, "", "LOGS       ");
    ExpectMount();
    auto entriesPerCluster = builder->Geometry().sectorsPerCluster * 16;

    for (uint32_t index = 0; index != entriesPerCluster - 2; ++index)
    {
        std::array<char, 16> path{ '/', 'l', 'o', 'g', 's', '/', 'f', static_cast<char>('0' + index / 10), static_cast<char>('0' + index % 10), '.', 't', 'x', 't' };
        auto file = ExpectOpen(infra::BoundedConstString(path.data(), 13), fs::OpenMode::write);
        ExpectClose(*file);
    }

    auto file = ExpectOpen("/logs/last.txt", fs::OpenMode::write);
    ExpectClose(*file);
    ExpectUnmount();

    fs::FatImageBuilder image{ device };
    auto second = image.FatEntry(logs);
    ASSERT_TRUE(image.Geometry().IsCluster(second));
    EXPECT_TRUE(fs::IsEndOfChain(image.Geometry().type, image.FatEntry(second)));
    ExpectMount();
    ExpectStat("/logs/last.txt", fs::Result::success, fs::EntryInfo{ "last.txt", fs::EntryType::file, 0 });
}

TEST_F(FatLiteTest, unmount_commits_the_open_files)
{
    auto contents = Pattern<700>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    ExpectWrite(*file, infra::MakeRange(contents));

    ExpectUnmount();
    ExpectSync(*file, fs::Result::notMounted);
    file = nullptr;

    ExpectContentsOnFullVolume("/log.txt", contents);
}

TEST_F(FatLiteOnFat32Test, unmount_writes_fsinfo_with_an_unknown_count_when_the_fat_changed)
{
    auto fsInfoSector = *builder->Geometry().fsInfoSector;
    auto before = builder->ReadSector(fsInfoSector);
    ExpectMount();
    ExpectUnmount();
    EXPECT_EQ(before, builder->ReadSector(fsInfoSector));

    auto contents = Pattern<700>();
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(contents));
    ExpectUnmount();

    auto after = builder->ReadSector(fsInfoSector);
    EXPECT_EQ(0xffffffff, fs::LoadLittle32(infra::MakeRange(after), 488));
    EXPECT_TRUE(builder->Geometry().IsCluster(fs::LoadLittle32(infra::MakeRange(after), 492)));
}

TEST_F(FatLiteOnFat12Test, the_allocator_skips_clusters_whose_entry_straddles_two_sectors)
{
    ASSERT_TRUE(fs::LocateEntry(fs::FatType::fat12, 341, 512).straddles);
    for (uint32_t cluster = 2; cluster != 340; ++cluster)
        builder->SetFatEntry(cluster, fs::EndOfChain(fs::FatType::fat12));

    auto contents = Pattern<1500>();
    ExpectMount();
    WriteFile("/big.bin", infra::MakeRange(contents));
    ExpectUnmount();

    fs::FatImageBuilder image{ device };
    EXPECT_EQ(342u, image.FatEntry(340));
    EXPECT_EQ(0u, image.FatEntry(341));
    EXPECT_EQ(343u, image.FatEntry(342));
    ExpectContentsOnFullVolume("/big.bin", contents);
}

TEST_F(FatLiteTest, a_full_volume_has_no_space)
{
    UseEveryClusterFrom(5);
    auto contents = Pattern<4096>();
    ExpectMount();
    auto file = ExpectOpen("/big.bin", fs::OpenMode::write);

    ExpectWrite(*file, infra::MakeRange(contents), fs::Result::noSpace);
    EXPECT_EQ(1536u, file->Size());
    EXPECT_EQ(1536u, file->Position());
    EXPECT_EQ(0u, FreeClusters());
    ExpectClose(*file);
    ExpectStat("/big.bin", fs::Result::success, fs::EntryInfo{ "big.bin", fs::EntryType::file, 1536 });
}

TEST_F(FatLiteTest, a_removed_medium_faults_the_volume)
{
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    auto contents = Pattern<100>();

    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    ExpectWrite(*file, infra::MakeRange(contents), fs::Result::mediaRemoved);
    ExpectStat("/log.txt", fs::Result::mediaRemoved);
    ExpectWrite(*file, infra::MakeRange(contents), fs::Result::mediaRemoved);
    ExpectUnmount(fs::Result::mediaRemoved);

    ExpectMount();
    ExpectWrite(*file, infra::MakeRange(contents), fs::Result::mediaRemoved);
    ExpectStat("/log.txt", fs::Result::notFound);
}

TEST_F(FatLiteTest, a_path_call_waiting_while_the_medium_is_removed_completes_with_media_removed)
{
    auto contents = Pattern<100>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);

    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    file->Write(infra::MakeRange(contents), Done());
    volume.Stat("/log.txt", [this](fs::Result result, const fs::EntryInfo&)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::mediaRemoved, 2);
}

TEST_F(FatLiteTest, open_directory_refuses_files_bad_directories_and_an_exhausted_pool)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    fs::ShortEntry bad;
    bad.name = fs::ShortName{ { 'B', 'A', 'D', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } };
    bad.attributes = fs::FatAttributes::directory;
    bad.firstCluster = builder->Geometry().clusterCount + 10;
    builder->AddEntries(0, "", bad);
    ExpectMount();

    infra::SharedPtr<fs::Directory> first;
    auto openDirectory = [this, &first](infra::BoundedConstString path, fs::Result expected)
    {
        volume.OpenDirectory(path, [this, &first](fs::Result result, infra::SharedPtr<fs::Directory> directory)
            {
                if (directory)
                    first = directory;

                done.callback(result);
            });
        ExpectDone(expected);
    };

    openDirectory("/file.txt", fs::Result::notADirectory);
    openDirectory("/bad", fs::Result::corrupted);
    openDirectory("/", fs::Result::success);
    openDirectory("/", fs::Result::tooManyOpenFiles);
}

TEST_F(FatLiteTest, a_parent_with_an_invalid_cluster_is_corrupted)
{
    fs::ShortEntry bad;
    bad.name = fs::ShortName{ { 'B', 'A', 'D', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } };
    bad.attributes = fs::FatAttributes::directory;
    bad.firstCluster = builder->Geometry().clusterCount + 10;
    builder->AddEntries(0, "", bad);
    ExpectMount();

    ExpectOpen("/bad/a.txt", fs::OpenMode::write, fs::Result::corrupted);
}

TEST_F(FatLiteTest, a_file_with_an_invalid_first_cluster_is_corrupted)
{
    fs::ShortEntry bad;
    bad.name = fs::ShortName{ { 'B', 'A', 'D', ' ', ' ', ' ', ' ', ' ', 'B', 'I', 'N' } };
    bad.firstCluster = builder->Geometry().clusterCount + 10;
    bad.size = 100;
    builder->AddEntries(0, "", bad);
    ExpectMount();

    ExpectOpen("/bad.bin", fs::OpenMode::read, fs::Result::corrupted);
}

TEST_F(FatLiteTest, a_full_directory_on_a_full_volume_has_no_space)
{
    auto logs = builder->AddDirectory(0, "", "LOGS       ");
    for (uint32_t index = 0; index != builder->Geometry().sectorsPerCluster * 16 - 2; ++index)
    {
        char name[12] = "F00     TXT";
        name[1] = static_cast<char>('0' + index / 10);
        name[2] = static_cast<char>('0' + index % 10);
        builder->AddFile(logs, "", name, infra::ConstByteRange());
    }
    UseEveryClusterFrom(2);
    ExpectMount();

    ExpectOpen("/logs/new.txt", fs::OpenMode::write, fs::Result::noSpace);
}

TEST_F(FatLiteTest, mounting_twice_is_busy_while_mounting)
{
    volume.Mount(Done());
    volume.Mount(Done());
    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();

    volume.Unmount(Done());
    volume.Unmount(Done());
    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatLiteTest, a_failed_mount_faults_until_the_next_mount)
{
    std::array<uint8_t, 512> zeros{};
    std::array<uint8_t, 512> first{};
    device.ReadBlock(0, infra::MakeRange(first));
    device.WriteBlock(0, infra::MakeRange(zeros));

    ExpectMount(fs::Result::corrupted);
    ExpectStat("/a.txt", fs::Result::notMounted);

    device.WriteBlock(0, infra::MakeRange(first));
    ExpectMount();
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}
