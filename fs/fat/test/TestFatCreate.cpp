#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/fat/FatTimestamp.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include "infra/timer/DerivedTimerService.hpp"
#include "infra/timer/PartitionedTime.hpp"
#include <algorithm>
#include <array>
#include <chrono>

namespace
{
    constexpr uint32_t localTimeTimerServiceId = 7;

    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern(uint8_t seed)
    {
        std::array<uint8_t, Size> pattern;

        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 7 + seed);

        return pattern;
    }

    class FatCreateTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatCreateTest(uint32_t blocks = 16384, const fs::FatFormatConfig& config = ClusterSize(1))
            : FatFileSystemFixture(blocks, config)
        {}

        void SetUp() override
        {
            builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(notes));
            documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
            ExpectMount();
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void AddFiles(uint32_t directory, uint32_t count)
        {
            for (uint32_t index = 0; index != count; ++index)
            {
                char name[12] = "F0000000TXT";
                name[6] = static_cast<char>('0' + index / 10);
                name[7] = static_cast<char>('0' + index % 10);
                builder->AddFile(directory, "", name, infra::ConstByteRange());
            }
        }

        fs::ShortEntry RawEntry(fs::FatEntryPosition position) const
        {
            auto sector = builder->ReadSector(builder->SectorOf(position));
            return fs::DecodeShortEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), position.index % 16 * 32), 32));
        }

        fs::EntryKind KindAt(fs::FatEntryPosition position) const
        {
            auto sector = builder->ReadSector(builder->SectorOf(position));
            return fs::KindOf(infra::Head(infra::DiscardHead(infra::MakeRange(sector), position.index % 16 * 32), 32));
        }

        std::array<uint8_t, 700> notes{ Pattern<700>(1) };
        uint32_t documents{ 0 };
    };

    class FatCreateWithSmallRootTest
        : public FatCreateTest
    {
    public:
        FatCreateWithSmallRootTest()
            : FatCreateTest(16384, SmallRoot())
        {}

        static fs::FatFormatConfig SmallRoot()
        {
            auto config = ClusterSize(1);
            config.rootEntries = 16;
            return config;
        }
    };

    class FatCreateOnFat32Test
        : public FatCreateTest
    {
    public:
        FatCreateOnFat32Test()
            : FatCreateTest(81920)
        {}
    };
}

TEST_F(FatCreateTest, write_mode_creates_an_empty_file)
{
    auto file = ExpectOpen("/new.txt", fs::OpenMode::write);

    EXPECT_EQ(0u, file->Size());
    EXPECT_EQ(0u, file->Position());
    ExpectStat("/new.txt", fs::Result::success, fs::EntryInfo{ "new.txt", fs::EntryType::file, 0 });
}

TEST_F(FatCreateTest, append_and_read_write_modes_create_too)
{
    ExpectOpen("/appended.log", fs::OpenMode::append);
    ExpectOpen("/both.dat", fs::OpenMode::readWrite);

    ExpectStat("/appended.log", fs::Result::success, fs::EntryInfo{ "appended.log", fs::EntryType::file, 0 });
    ExpectStat("/both.dat", fs::Result::success, fs::EntryInfo{ "both.dat", fs::EntryType::file, 0 });
}

TEST_F(FatCreateTest, read_mode_does_not_create)
{
    ExpectOpen("/new.txt", fs::OpenMode::read, fs::Result::notFound);

    ExpectStat("/new.txt", fs::Result::notFound);
}

TEST_F(FatCreateTest, existing_file_is_opened_instead_of_created)
{
    auto file = ExpectOpen("/NOTES.TXT", fs::OpenMode::append);

    EXPECT_EQ(700u, file->Size());
}

TEST_F(FatCreateTest, name_that_fits_8_3_is_one_short_entry_with_its_case)
{
    ExpectOpen("/readme.TXT", fs::OpenMode::write);
    ExpectUnmount();

    EXPECT_EQ(fs::EntryKind::file, KindAt(fs::FatEntryPosition{ 0, 4 }));
    auto entry = RawEntry(fs::FatEntryPosition{ 0, 4 });
    EXPECT_EQ((fs::ShortName{ { 'R', 'E', 'A', 'D', 'M', 'E', ' ', ' ', 'T', 'X', 'T' }, fs::ShortName::lowerCaseBase }), entry.name);
    EXPECT_EQ(fs::FatAttributes::archive, entry.attributes);
    EXPECT_EQ(fs::EntryKind::end, KindAt(fs::FatEntryPosition{ 0, 5 }));
}

TEST_F(FatCreateTest, long_name_gets_the_lowest_free_alias)
{
    ExpectOpen("/Long File Name.txt", fs::OpenMode::write);
    ExpectOpen("/Long File Name 2.txt", fs::OpenMode::write);

    ExpectStat("/long file name.txt", fs::Result::success, fs::EntryInfo{ "Long File Name.txt", fs::EntryType::file, 0 });
    ExpectStat("/LONGFI~1.TXT", fs::Result::success, fs::EntryInfo{ "Long File Name.txt", fs::EntryType::file, 0 });
    ExpectStat("/LONGFI~2.TXT", fs::Result::success, fs::EntryInfo{ "Long File Name 2.txt", fs::EntryType::file, 0 });
}

TEST_F(FatCreateTest, name_with_characters_a_short_name_cannot_hold_is_a_long_name)
{
    ExpectOpen("/a+b.txt", fs::OpenMode::write);

    ExpectStat("/A_B~1.TXT", fs::Result::success, fs::EntryInfo{ "a+b.txt", fs::EntryType::file, 0 });
}

TEST_F(FatCreateTest, hashed_alias_is_used_when_every_tail_is_taken)
{
    auto basis = fs::BasisName("long file name.txt");
    for (uint32_t tail = 1; tail <= 32; ++tail)
    {
        char name[12];
        auto alias = fs::WithNumericTail(basis, tail);
        std::copy(alias.begin(), alias.end(), name);
        name[11] = 0;
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }

    ExpectOpen("/long file name.txt", fs::OpenMode::write);

    auto hashed = fs::HashedAlias(basis, "long file name.txt");
    std::array<char, 13> path{ '/' };
    std::copy_n(hashed.begin(), 8, path.begin() + 1);
    path[9] = '.';
    std::copy_n(hashed.begin() + 8, 3, path.begin() + 10);
    ExpectStat(infra::BoundedConstString(path.data(), path.size()), fs::Result::success, fs::EntryInfo{ "long file name.txt", fs::EntryType::file, 0 });
}

TEST_F(FatCreateTest, used_hashed_alias_is_no_space)
{
    auto basis = fs::BasisName("long file name.txt");
    for (uint32_t tail = 1; tail <= 32; ++tail)
    {
        char name[12];
        auto alias = fs::WithNumericTail(basis, tail);
        std::copy(alias.begin(), alias.end(), name);
        name[11] = 0;
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }
    char hashed[12];
    auto alias = fs::HashedAlias(basis, "long file name.txt");
    std::copy(alias.begin(), alias.end(), hashed);
    hashed[11] = 0;
    builder->AddFile(0, "", hashed, infra::ConstByteRange());

    ExpectOpen("/long file name.txt", fs::OpenMode::write, fs::Result::noSpace);
}

TEST_F(FatCreateTest, file_is_created_in_a_subdirectory)
{
    ExpectOpen("/Documents/report.txt", fs::OpenMode::write);

    ExpectStat("/documents/REPORT.TXT", fs::Result::success, fs::EntryInfo{ "report.txt", fs::EntryType::file, 0 });
    ExpectStat("/report.txt", fs::Result::notFound);
}

TEST_F(FatCreateTest, missing_parent_is_not_found)
{
    ExpectOpen("/missing/new.txt", fs::OpenMode::write, fs::Result::notFound);
    ExpectOpen("/notes.txt/new.txt", fs::OpenMode::write, fs::Result::notADirectory);
}

TEST_F(FatCreateTest, invalid_names_are_rejected)
{
    ExpectOpen("/a*b", fs::OpenMode::write, fs::Result::invalidPath);
    ExpectOpen("/name.", fs::OpenMode::write, fs::Result::invalidPath);
    ExpectOpen("/name ", fs::OpenMode::write, fs::Result::invalidPath);
    ExpectOpen("/", fs::OpenMode::write, fs::Result::isADirectory);
}

TEST_F(FatCreateTest, too_long_name_is_rejected)
{
    std::array<char, 257> path;
    path.fill('n');
    path[0] = '/';

    ExpectOpen(infra::BoundedConstString(path.data(), path.size()), fs::OpenMode::write, fs::Result::nameTooLong);
}

TEST_F(FatCreateTest, created_file_is_written_and_read_back)
{
    auto data = Pattern<1500>(3);
    auto file = ExpectOpen("/data.bin", fs::OpenMode::write);
    file->Write(infra::MakeRange(data), Done());
    ExpectDone(fs::Result::success);
    file->Close(Done());
    ExpectDone(fs::Result::success);
    file = nullptr;
    ExpectUnmount();
    ExpectMount();

    file = ExpectOpen("/data.bin");
    std::array<uint8_t, 1501> read{};
    ExpectRead(*file, infra::MakeRange(read), fs::Result::success, 1500);
    EXPECT_TRUE(std::equal(data.begin(), data.end(), read.begin()));
}

TEST_F(FatCreateTest, created_file_survives_an_unmount_without_close)
{
    ExpectOpen("/kept.txt", fs::OpenMode::write);

    ExpectUnmount();
    ExpectMount();

    ExpectStat("/kept.txt", fs::Result::success, fs::EntryInfo{ "kept.txt", fs::EntryType::file, 0 });
}

TEST_F(FatCreateTest, created_file_that_is_open_cannot_be_opened_again)
{
    auto file = ExpectOpen("/new.txt", fs::OpenMode::write);

    ExpectOpen("/new.txt", fs::OpenMode::read, fs::Result::busy);
}

TEST_F(FatCreateTest, nothing_is_created_when_the_pool_is_exhausted)
{
    auto first = ExpectOpen("/notes.txt");
    auto second = ExpectOpen("/notes.txt");

    ExpectOpen("/new.txt", fs::OpenMode::write, fs::Result::tooManyOpenFiles);

    ExpectStat("/new.txt", fs::Result::notFound);
}

TEST_F(FatCreateTest, full_directory_grows)
{
    AddFiles(documents, 14);

    ExpectOpen("/Documents/new.txt", fs::OpenMode::write);
    ExpectUnmount();

    EXPECT_TRUE(builder->Geometry().IsCluster(builder->FatEntry(documents)));
    ExpectMount();
    ExpectStat("/Documents/new.txt", fs::Result::success, fs::EntryInfo{ "new.txt", fs::EntryType::file, 0 });
    ExpectStat("/Documents/F0000013.TXT", fs::Result::success, fs::EntryInfo{ "F0000013.TXT", fs::EntryType::file, 0 });
}

TEST_F(FatCreateTest, parent_whose_cluster_is_outside_the_volume_is_corrupted)
{
    fs::ShortEntry entry;
    entry.name = fs::ShortName{ { 'B', 'A', 'D', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } };
    entry.attributes = fs::FatAttributes::directory;
    entry.firstCluster = builder->Geometry().clusterCount + 2;
    builder->AddEntries(0, "", entry);

    ExpectOpen("/bad/new.txt", fs::OpenMode::write, fs::Result::corrupted);
}

TEST_F(FatCreateTest, device_error_while_scanning_for_the_hashed_alias_is_reported)
{
    auto basis = fs::BasisName("long file name.txt");
    for (uint32_t tail = 1; tail <= 32; ++tail)
    {
        char name[12];
        auto alias = fs::WithNumericTail(basis, tail);
        std::copy(alias.begin(), alias.end(), name);
        name[11] = 0;
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }
    AddFiles(0, 60);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 7);

    ExpectOpen("/long file name.txt", fs::OpenMode::write, fs::Result::deviceError);
}

TEST_F(FatCreateTest, device_error_while_scanning_the_parent_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectOpen("/new.txt", fs::OpenMode::write, fs::Result::deviceError);
}

TEST_F(FatCreateWithSmallRootTest, full_fixed_root_is_no_space)
{
    AddFiles(0, 12);

    ExpectOpen("/new.txt", fs::OpenMode::write, fs::Result::noSpace);
}

TEST_F(FatCreateOnFat32Test, root_directory_of_fat32_holds_new_files)
{
    ExpectOpen("/A Long Name On Fat32.txt", fs::OpenMode::write);
    ExpectUnmount();
    ExpectMount();

    ExpectStat("/a long name on fat32.txt", fs::Result::success, fs::EntryInfo{ "A Long Name On Fat32.txt", fs::EntryType::file, 0 });
}

namespace
{
    class FatCreateTimestampTest
        : public FatFileSystemFixture
    {
    public:
        FatCreateTimestampTest()
        {
            localTime.Shift(infra::PartitionedTime(2026, 10, 12, 16, 21, 34).ToTimePoint() - infra::TimePoint());
        }

        static fs::FatConfig LocalTime()
        {
            fs::FatConfig config;
            config.timerServiceId = localTimeTimerServiceId;
            return config;
        }

        void MountTimed()
        {
            timed.Mount([this](fs::Result result)
                {
                    lifecycleDone.callback(result);
                });
            EXPECT_CALL(lifecycleDone, callback(fs::Result::success));
            ExecuteAllActions();
        }

        void UnmountTimed()
        {
            timed.Unmount([this](fs::Result result)
                {
                    lifecycleDone.callback(result);
                });
            EXPECT_CALL(lifecycleDone, callback(fs::Result::success));
            ExecuteAllActions();
        }

        infra::SharedPtr<fs::File> CreateTimed(infra::BoundedConstString path)
        {
            infra::SharedPtr<fs::File> opened;
            timed.Open(path, fs::OpenMode::write, [this, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
                {
                    opened = file;
                    openDone.callback(result);
                });
            EXPECT_CALL(openDone, callback(fs::Result::success));
            ExecuteAllActions();
            return opened;
        }

        fs::ShortEntry RawEntry(fs::FatEntryPosition position) const
        {
            auto sector = builder->ReadSector(builder->SectorOf(position));
            return fs::DecodeShortEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), position.index % 16 * 32), 32));
        }

        infra::DerivedTimerService localTime{ localTimeTimerServiceId, systemTimerService };
        fs::FatFileSystem::WithStorage<2, 4> timed{ device, LocalTime() };
    };
}

TEST_F(FatCreateTimestampTest, creation_time_comes_from_the_configured_timer_service)
{
    MountTimed();
    CreateTimed("/new.txt");
    UnmountTimed();

    auto expected = fs::ToFatDateTime(infra::PartitionedTime(2026, 10, 12, 16, 21, 34).ToTimePoint());
    auto entry = RawEntry(fs::FatEntryPosition{ 0, 0 });
    EXPECT_EQ(expected.date, entry.createDate);
    EXPECT_EQ(expected.time, entry.createTime);
    EXPECT_EQ(expected.date, entry.accessDate);
    EXPECT_EQ(expected.date, entry.writeDate);
    EXPECT_EQ(expected.time, entry.writeTime);
}

TEST_F(FatCreateTimestampTest, sync_after_a_write_updates_the_write_time)
{
    MountTimed();
    auto file = CreateTimed("/new.txt");
    ForwardTime(std::chrono::seconds(10));
    std::array<uint8_t, 4> data{ 1, 2, 3, 4 };
    file->Write(infra::MakeRange(data), [this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::success);
    file->Sync([this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::success);

    auto created = fs::ToFatDateTime(infra::PartitionedTime(2026, 10, 12, 16, 21, 34).ToTimePoint());
    auto written = fs::ToFatDateTime(infra::PartitionedTime(2026, 10, 12, 16, 21, 44).ToTimePoint());
    auto entry = RawEntry(fs::FatEntryPosition{ 0, 0 });
    EXPECT_EQ(created.time, entry.createTime);
    EXPECT_EQ(written.time, entry.writeTime);
    EXPECT_EQ(written.date, entry.writeDate);
}

TEST_F(FatCreateTest, default_timer_service_dates_files_1980)
{
    ExpectOpen("/new.txt", fs::OpenMode::write);
    ExpectUnmount();

    auto entry = RawEntry(fs::FatEntryPosition{ 0, 4 });
    EXPECT_EQ((1 << 5) | 1, entry.createDate);
    EXPECT_EQ(0, entry.createTime);
}
