#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace
{
    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern(uint8_t seed)
    {
        std::array<uint8_t, Size> pattern;

        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 13 + seed + index / 509);

        return pattern;
    }

    class FatWriteTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatWriteTest(uint32_t blocks = 16384)
            : FatFileSystemFixture(blocks)
        {}

        void SetUp() override
        {
            builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(original));
            builder->AddFile(0, "", "EMPTY   TXT", infra::ConstByteRange());
            ExpectMount();
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void ExpectWrite(fs::File& file, infra::ConstByteRange data, fs::Result result = fs::Result::success)
        {
            file.Write(data, Done());
            ExpectDone(result);
        }

        void ExpectSync(fs::File& file, fs::Result result = fs::Result::success)
        {
            file.Sync(Done());
            ExpectDone(result);
        }

        void Remount()
        {
            ExpectUnmount();
            ExpectMount();
        }

        template<std::size_t Size>
        void ExpectContents(infra::BoundedConstString path, const std::array<uint8_t, Size>& expected)
        {
            auto file = ExpectOpen(path);
            ASSERT_NE(nullptr, file);
            EXPECT_EQ(Size, file->Size());
            std::array<uint8_t, Size + 1> read{};
            ExpectRead(*file, infra::MakeRange(read), fs::Result::success, Size);
            EXPECT_TRUE(std::equal(expected.begin(), expected.end(), read.begin()));
        }

        std::array<uint8_t, 700> original{ Pattern<700>(1) };
    };
}

TEST_F(FatWriteTest, overwrite_inside_a_file_keeps_its_size)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto update = Pattern<100>(7);

    ExpectWrite(*file, infra::MakeRange(update));

    EXPECT_EQ(100u, file->Position());
    EXPECT_EQ(700u, file->Size());
    file = nullptr;
    Remount();
    auto expected = original;
    std::copy(update.begin(), update.end(), expected.begin());
    ExpectContents("/notes.txt", expected);
}

TEST_F(FatWriteTest, append_within_the_last_cluster_needs_no_allocation)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<100>(9);

    ExpectWrite(*file, infra::MakeRange(added));
    ExpectSync(*file);

    EXPECT_EQ(800u, file->Size());
    file = nullptr;
    Remount();
    std::array<uint8_t, 800> expected;
    std::copy(original.begin(), original.end(), expected.begin());
    std::copy(added.begin(), added.end(), expected.begin() + 700);
    ExpectContents("/notes.txt", expected);
}

TEST_F(FatWriteTest, append_across_a_cluster_boundary_links_a_new_cluster)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<400>(3);

    ExpectWrite(*file, infra::MakeRange(added));
    ExpectSync(*file);

    file = nullptr;
    Remount();
    std::array<uint8_t, 1100> expected;
    std::copy(original.begin(), original.end(), expected.begin());
    std::copy(added.begin(), added.end(), expected.begin() + 700);
    ExpectContents("/notes.txt", expected);
}

TEST_F(FatWriteTest, first_write_to_an_empty_file_gives_it_a_chain)
{
    auto file = ExpectOpen("/empty.txt", fs::OpenMode::append);
    auto added = Pattern<10>(5);

    ExpectWrite(*file, infra::MakeRange(added));
    ExpectSync(*file);

    file = nullptr;
    Remount();
    ExpectContents("/empty.txt", added);
}

TEST_F(FatWriteTest, read_after_a_write_sees_the_new_data_before_sync)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto update = Pattern<600>(11);
    ExpectWrite(*file, infra::MakeRange(update));

    file->Seek(0);
    std::array<uint8_t, 700> read{};
    ExpectRead(*file, infra::MakeRange(read), fs::Result::success, 700);

    EXPECT_TRUE(std::equal(update.begin(), update.end(), read.begin()));
    EXPECT_TRUE(std::equal(original.begin() + 600, original.end(), read.begin() + 600));
}

TEST_F(FatWriteTest, small_writes_stay_in_the_cache_until_sync)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto writes = device.WriteRequests();
    auto update = Pattern<64>(2);

    for (int chunk = 0; chunk != 4; ++chunk)
        ExpectWrite(*file, infra::MakeRange(update));

    EXPECT_EQ(writes, device.WriteRequests());
    ExpectSync(*file);
    EXPECT_EQ(writes + 1, device.WriteRequests());
}

TEST_F(FatWriteTest, long_run_of_whole_sectors_is_written_directly_in_one_request)
{
    auto file = ExpectOpen("/empty.txt", fs::OpenMode::append);
    auto data = Pattern<4096>(4);
    infra::BoundedVector<uint32_t>::WithMaxSize<16> writes;
    device.Observe([&writes](fs::SparseBlockDevice::Request request, uint32_t block)
        {
            if (request == fs::SparseBlockDevice::Request::write)
                writes.push_back(block);
        });

    ExpectWrite(*file, infra::MakeRange(data));

    ASSERT_EQ(1u, writes.size());
    EXPECT_EQ(builder->Geometry().SectorOfCluster(4), writes[0]);
}

TEST_F(FatWriteTest, sync_without_changes_writes_nothing)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto writes = device.WriteRequests();
    auto flushes = device.FlushRequests();

    ExpectSync(*file);

    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(flushes, device.FlushRequests());
}

TEST_F(FatWriteTest, overwrite_that_keeps_the_size_does_not_write_the_entry)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto update = Pattern<512>(8);
    ExpectWrite(*file, infra::MakeRange(update));
    auto writes = device.WriteRequests();

    ExpectSync(*file);

    EXPECT_EQ(writes + 1, device.WriteRequests());
}

TEST_F(FatWriteTest, close_commits_the_file)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<10>(6);
    ExpectWrite(*file, infra::MakeRange(added));

    file->Close(Done());
    ExpectDone(fs::Result::success);
    file = nullptr;

    Remount();
    std::array<uint8_t, 710> expected;
    std::copy(original.begin(), original.end(), expected.begin());
    std::copy(added.begin(), added.end(), expected.begin() + 700);
    ExpectContents("/notes.txt", expected);
}

TEST_F(FatWriteTest, released_file_is_committed_in_the_background)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<10>(6);
    ExpectWrite(*file, infra::MakeRange(added));

    file = nullptr;
    ExecuteAllActions();

    Remount();
    std::array<uint8_t, 710> expected;
    std::copy(original.begin(), original.end(), expected.begin());
    std::copy(added.begin(), added.end(), expected.begin() + 700);
    ExpectContents("/notes.txt", expected);
}

TEST_F(FatWriteTest, released_file_returns_to_the_pool_after_its_commit)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto other = ExpectOpen("/empty.txt", fs::OpenMode::append);
    auto added = Pattern<10>(6);
    ExpectWrite(*file, infra::MakeRange(added));
    file = nullptr;

    file = ExpectOpen("/notes.txt");

    EXPECT_EQ(710u, file->Size());
}

TEST_F(FatWriteTest, unmount_commits_open_files)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<10>(6);
    ExpectWrite(*file, infra::MakeRange(added));

    Remount();

    std::array<uint8_t, 710> expected;
    std::copy(original.begin(), original.end(), expected.begin());
    std::copy(added.begin(), added.end(), expected.begin() + 700);
    ExpectContents("/notes.txt", expected);
    ExpectWrite(*file, infra::MakeRange(added), fs::Result::notMounted);
}

TEST_F(FatWriteTest, file_opened_for_reading_cannot_be_written)
{
    auto file = ExpectOpen("/notes.txt");
    auto added = Pattern<10>(6);

    ExpectWrite(*file, infra::MakeRange(added), fs::Result::readOnly);
}

TEST_F(FatWriteTest, calls_while_a_write_is_pending_are_busy)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto added = Pattern<10>(6);
    std::array<uint8_t, 10> buffer{};

    file->Write(infra::MakeRange(added), Done());
    file->Write(infra::MakeRange(added), Done());
    file->Sync(Done());
    file->Close(Done());
    StartRead(*file, infra::MakeRange(buffer));

    EXPECT_CALL(done, callback(fs::Result::busy)).Times(3);
    EXPECT_CALL(readDone, callback(fs::Result::busy, 0));
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatWriteTest, failed_write_keeps_the_bytes_already_written)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<2000>(6);
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 2);

    ExpectWrite(*file, infra::MakeRange(added), fs::Result::deviceError);

    EXPECT_LT(700u, file->Size());
    EXPECT_GT(2700u, file->Size());
    EXPECT_EQ(file->Size(), file->Position());
}

TEST_F(FatWriteTest, removed_card_during_a_write_faults_the_volume)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<2000>(6);
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ExpectWrite(*file, infra::MakeRange(added), fs::Result::mediaRemoved);

    ExpectStat("/", fs::Result::mediaRemoved);
}

TEST_F(FatWriteTest, write_protected_card_is_read_only)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto update = Pattern<10>(6);
    ExpectWrite(*file, infra::MakeRange(update));
    device.FailNextOperationWith(hal::BlockDevice::Result::writeProtected);

    ExpectSync(*file, fs::Result::readOnly);
}

TEST_F(FatWriteTest, sync_that_cannot_read_the_entry_is_reported)
{
    auto big = Pattern<4096>(2);
    builder->AddFile(0, "", "BIG     BIN", infra::MakeRange(big));
    auto file = ExpectOpen("/big.bin", fs::OpenMode::readWrite);
    std::array<uint8_t, 1> byte{ 0x55 };
    for (uint32_t sector = 0; sector != 4; ++sector)
    {
        file->Seek(sector * 512 + 1);
        ExpectWrite(*file, infra::MakeRange(byte));
    }
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ExpectSync(*file, fs::Result::deviceError);
}

TEST_F(FatWriteTest, unmount_that_cannot_commit_a_file_is_reported)
{
    auto big = Pattern<4096>(2);
    builder->AddFile(0, "", "BIG     BIN", infra::MakeRange(big));
    auto file = ExpectOpen("/big.bin", fs::OpenMode::readWrite);
    std::array<uint8_t, 1> byte{ 0x55 };
    for (uint32_t sector = 0; sector != 4; ++sector)
    {
        file->Seek(sector * 512 + 1);
        ExpectWrite(*file, infra::MakeRange(byte));
    }
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ExpectUnmount(fs::Result::deviceError);
}

TEST_F(FatWriteTest, append_to_a_chain_shorter_than_its_file_is_corrupted)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'S', 'H', 'O', 'R', 'T', ' ', ' ', ' ', 'B', 'I', 'N' };
    entry.attributes = fs::FatAttributes::archive;
    entry.firstCluster = builder->AllocateChain(1);
    entry.size = 1536;
    builder->AddEntries(0, "", entry);
    auto file = ExpectOpen("/short.bin", fs::OpenMode::append);
    auto added = Pattern<600>(6);

    ExpectWrite(*file, infra::MakeRange(added), fs::Result::corrupted);
}

TEST_F(FatWriteTest, failure_to_allocate_is_reported)
{
    auto file = ExpectOpen("/empty.txt", fs::OpenMode::append);
    auto added = Pattern<10>(6);
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ExpectWrite(*file, infra::MakeRange(added), fs::Result::deviceError);

    EXPECT_EQ(0u, file->Size());
}

TEST_F(FatWriteTest, write_on_a_nearly_full_volume_writes_what_fits)
{
    builder->AllocateChain(builder->Geometry().clusterCount - 4);
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<2000>(6);

    ExpectWrite(*file, infra::MakeRange(added), fs::Result::noSpace);

    EXPECT_EQ(4u * 512, file->Size());
    ExpectWrite(*file, infra::MakeRange(added), fs::Result::noSpace);
    EXPECT_EQ(4u * 512, file->Size());
}

TEST_F(FatWriteTest, sector_after_the_end_of_a_file_is_zero_beyond_the_written_bytes)
{
    std::array<uint8_t, 512> stale;
    stale.fill(0xee);
    device.WriteBlock(builder->Geometry().SectorOfCluster(4), infra::MakeRange(stale));
    auto file = ExpectOpen("/empty.txt", fs::OpenMode::append);
    auto added = Pattern<10>(6);
    ExpectWrite(*file, infra::MakeRange(added));
    ExpectSync(*file);

    auto sector = builder->ReadSector(builder->Geometry().SectorOfCluster(4));

    EXPECT_TRUE(std::equal(added.begin(), added.end(), sector.begin()));
    EXPECT_TRUE(std::all_of(sector.begin() + 10, sector.end(), [](uint8_t byte)
        {
            return byte == 0;
        }));
}

TEST_F(FatWriteTest, few_whole_sectors_are_collected_in_the_cache)
{
    auto file = ExpectOpen("/empty.txt", fs::OpenMode::append);
    auto data = Pattern<1024>(4);
    auto writes = device.WriteRequests();

    ExpectWrite(*file, infra::MakeRange(data));

    EXPECT_EQ(writes, device.WriteRequests());
}

TEST_F(FatWriteTest, sync_after_a_write_sets_the_archive_attribute)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'P', 'L', 'A', 'I', 'N', ' ', ' ', ' ', 'B', 'I', 'N' };
    auto position = builder->AddEntries(0, "", entry);
    auto file = ExpectOpen("/plain.bin", fs::OpenMode::append);
    auto added = Pattern<10>(6);

    ExpectWrite(*file, infra::MakeRange(added));
    ExpectSync(*file);

    auto sector = builder->ReadSector(builder->SectorOf(position));
    auto written = fs::DecodeShortEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), position.index % 16 * 32), 32));
    EXPECT_NE(0, written.attributes & fs::FatAttributes::archive);
    EXPECT_EQ(10u, written.size);
}

TEST_F(FatWriteTest, append_writes_at_the_end_whatever_the_position)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<10>(6);
    file->Seek(0);

    ExpectWrite(*file, infra::MakeRange(added));

    EXPECT_EQ(710u, file->Size());
    EXPECT_EQ(710u, file->Position());
}

TEST_F(FatWriteTest, whole_sector_overwrite_does_not_read_the_sector)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto update = Pattern<512>(8);
    file->Seek(512);
    auto reads = device.ReadRequests();

    ExpectWrite(*file, infra::MakeRange(update));

    EXPECT_EQ(reads + 1, device.ReadRequests());
    EXPECT_EQ(1024u, file->Size());
}

TEST_F(FatWriteTest, released_file_writes_back_an_overwrite)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto update = Pattern<10>(8);
    ExpectWrite(*file, infra::MakeRange(update));

    file = nullptr;
    ExecuteAllActions();

    auto sector = builder->ReadSector(builder->Geometry().SectorOfCluster(2));
    EXPECT_TRUE(std::equal(update.begin(), update.end(), sector.begin()));
}

TEST_F(FatWriteTest, truncate_and_expect_size_are_not_supported_yet)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);

    file->Truncate(Done());
    file->ExpectSize(1000, Done());
    EXPECT_CALL(done, callback(fs::Result::unsupported)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatWriteTest, append_after_a_reopen_does_not_walk_the_chain)
{
    for (uint32_t index = 0; index != 8; ++index)
        builder->SetFatEntry(1000 + index * 300, index == 7 ? fs::EndOfChain(builder->Geometry().type) : 1000 + (index + 1) * 300);
    fs::ShortEntry entry;
    entry.name.bytes = { 'S', 'P', 'R', 'E', 'A', 'D', ' ', ' ', 'B', 'I', 'N' };
    entry.attributes = fs::FatAttributes::archive;
    entry.firstCluster = 1000;
    entry.size = 8 * 512 - 100;
    builder->AddEntries(0, "", entry);
    auto file = ExpectOpen("/spread.bin");
    std::array<uint8_t, 8 * 512> buffer{};
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 8 * 512 - 100);
    file = nullptr;
    file = ExpectOpen("/spread.bin", fs::OpenMode::append);
    auto added = Pattern<50>(6);
    auto reads = device.ReadRequests();

    ExpectWrite(*file, infra::MakeRange(added));

    EXPECT_GE(reads + 1, device.ReadRequests());
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

    class FatWriteLimitTest
        : public FatFileSystemFixture
    {
    public:
        void SetUp() override
        {
            exposed.Mount([this](fs::Result result)
                {
                    done.callback(result);
                });
            ExpectDone(fs::Result::success);
        }

        ExposedFileSystem exposed{ device };
        fs::FatExtentCache::WithStorage<4> extents;
        fs::FatOpenFile file;
    };
}

TEST_F(FatWriteLimitTest, write_beyond_4_gib_minus_one_byte_stops_there_with_no_space)
{
    file.mode = fs::OpenMode::readWrite;
    file.firstCluster = 100;
    file.size = 0xfffffff0;
    file.position = file.size;
    file.clusters = file.size / 512 + 1;
    extents.Add(fs::FatExtent{ file.clusters - 1, 100, 1 });
    std::array<uint8_t, 32> data{};

    fs::FatWrite write{ exposed.Volume(), file, extents, infra::MakeRange(data), [this](fs::Result result)
        {
            done.callback(result);
        } };
    write.Start();
    ExpectDone(fs::Result::noSpace);

    EXPECT_EQ(0xffffffffu, file.size);
    EXPECT_EQ(0xffffffffu, file.position);
}

namespace
{
    class FatWriteOnFat32Test
        : public FatWriteTest
    {
    public:
        FatWriteOnFat32Test()
            : FatWriteTest(81920)
        {}

        void SetUp() override
        {
            builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(original));
        }

        std::optional<fs::FsInfo> FsInfoOnDevice() const
        {
            auto sector = builder->ReadSector(*builder->Geometry().fsInfoSector);
            return fs::ParseFsInfo(infra::MakeRange(sector), builder->Geometry().clusterCount);
        }
    };
}

TEST_F(FatWriteOnFat32Test, unmount_after_allocation_writes_fsinfo)
{
    auto before = FsInfoOnDevice();
    ExpectMount();
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<2000>(6);
    ExpectWrite(*file, infra::MakeRange(added));
    file = nullptr;

    ExpectUnmount();

    ASSERT_TRUE(before && before->freeClusters);
    auto after = FsInfoOnDevice();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*before->freeClusters - 4, after->freeClusters);
    EXPECT_NE(before->nextFree, after->nextFree);
}

TEST_F(FatWriteOnFat32Test, unmount_without_allocation_leaves_fsinfo_alone)
{
    ExpectMount();
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto update = Pattern<10>(6);
    ExpectWrite(*file, infra::MakeRange(update));
    file = nullptr;
    ExecuteAllActions();
    auto writes = device.WriteRequests();

    ExpectUnmount();

    EXPECT_EQ(writes, device.WriteRequests());
}

TEST_F(FatWriteOnFat32Test, unmount_of_a_volume_with_an_unknown_count_marks_the_count_unknown)
{
    builder->SetFatEntry(1, fs::WithCleanShutdown(fs::FatType::fat32, builder->FatEntry(1), false));
    ExpectMount();
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto added = Pattern<2000>(6);
    ExpectWrite(*file, infra::MakeRange(added));
    file = nullptr;

    ExpectUnmount();

    auto after = FsInfoOnDevice();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(std::nullopt, after->freeClusters);
}
