#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/lite/test/FatLiteFixture.hpp"
#include <algorithm>

namespace
{
    class FatLiteFileTest
        : public testing::Test
        , public FatLiteFixture
    {
    public:
        explicit FatLiteFileTest(const FatLiteVolumeCase& volumeCase = fat16Volume)
            : FatLiteFixture(volumeCase)
        {}

        std::array<uint8_t, 8192> buffer{};
    };

    class FatLiteFileWithLargeClustersTest
        : public FatLiteFileTest
    {
    public:
        FatLiteFileWithLargeClustersTest()
            : FatLiteFileTest(fat16WithLargeClusters)
        {}
    };

    class FatLiteFileWithTransferAlignmentTest
        : public testing::Test
        , public FatLiteFixture
    {
    public:
        FatLiteFileWithTransferAlignmentTest()
            : FatLiteFixture(fat16WithLargeClusters, fs::bufferAlignment)
        {}

        infra::ByteRange At(std::size_t offset, std::size_t size)
        {
            return infra::Head(infra::DiscardHead(infra::MakeRange(aligned), offset), size);
        }

        alignas(fs::bufferAlignment) std::array<uint8_t, 4096 + fs::bufferAlignment> aligned{};
    };

    class FatLiteFileWithOneFatTest
        : public FatLiteFileTest
    {
    public:
        FatLiteFileWithOneFatTest()
            : FatLiteFileTest(fat16WithOneFat)
        {}
    };
}

TEST_F(FatLiteFileTest, small_writes_fill_a_sector_and_read_back)
{
    auto contents = Pattern<30>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    for (std::size_t part = 0; part != 3; ++part)
        ExpectWrite(*file, infra::Head(infra::DiscardHead(infra::MakeRange(contents), part * 10), 10));

    EXPECT_EQ(30u, file->Size());
    EXPECT_EQ(30u, file->Position());
    file->Seek(0);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 30);
    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);
}

TEST_F(FatLiteFileWithLargeClustersTest, whole_sectors_of_a_cluster_are_written_in_one_request)
{
    auto contents = Pattern<4096>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    ExpectWrite(*file, infra::MakeRange(contents));

    auto writes = device.WriteRequests();
    ExpectWrite(*file, infra::MakeRange(contents));
    EXPECT_EQ(writes + 1, device.WriteRequests());

    ExpectClose(*file);
    ExpectUnmount();
    std::array<uint8_t, 8192> both;
    std::copy(contents.begin(), contents.end(), both.begin());
    std::copy(contents.begin(), contents.end(), both.begin() + 4096);
    ExpectContentsOnFullVolume("/log.txt", both);
}

TEST_F(FatLiteFileWithLargeClustersTest, a_direct_read_sees_a_sector_modified_in_the_window)
{
    auto contents = Pattern<4096>();
    auto patch = Pattern<10>(99);
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, infra::MakeRange(contents));
    file->Seek(600);
    ExpectWrite(*file, infra::MakeRange(patch));
    EXPECT_EQ(4096u, file->Size());

    auto reads = device.ReadRequests();
    file->Seek(0);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 4096);
    EXPECT_EQ(reads + 1, device.ReadRequests());
    std::copy(patch.begin(), patch.end(), contents.begin() + 600);
    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
}

TEST_F(FatLiteFileTest, append_writes_at_the_end_whatever_the_position)
{
    auto first = Pattern<100>();
    auto second = Pattern<50>(3);
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(first));

    auto file = ExpectOpen("/log.txt", fs::OpenMode::append);
    EXPECT_EQ(100u, file->Position());
    file->Seek(10);
    ExpectWrite(*file, infra::MakeRange(second));
    EXPECT_EQ(150u, file->Size());
    ExpectClose(*file);

    std::array<uint8_t, 150> both;
    std::copy(first.begin(), first.end(), both.begin());
    std::copy(second.begin(), second.end(), both.begin() + 100);
    ExpectContents("/log.txt", both);
}

TEST_F(FatLiteFileTest, seek_beyond_the_end_moves_to_the_end)
{
    auto contents = Pattern<100>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, infra::MakeRange(contents));

    file->Seek(1000);
    EXPECT_EQ(100u, file->Position());
}

TEST_F(FatLiteFileTest, write_mode_truncates_an_existing_file_and_frees_its_clusters)
{
    auto free = FreeClusters();
    auto contents = Pattern<5000>();
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(contents));
    ExpectUnmount();
    EXPECT_EQ(free - 10, FreeClusters());

    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    EXPECT_EQ(0u, file->Size());
    ExpectClose(*file);
    ExpectUnmount();

    EXPECT_EQ(free, FreeClusters());
    ExpectMount();
    ExpectStat("/log.txt", fs::Result::success, fs::EntryInfo{ "log.txt", fs::EntryType::file, 0 });
}

TEST_F(FatLiteFileTest, truncate_across_clusters_frees_the_cut_ones)
{
    auto free = FreeClusters();
    auto contents = Pattern<5000>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, infra::MakeRange(contents));
    file->Seek(1500);
    ExpectTruncate(*file);
    EXPECT_EQ(1500u, file->Size());
    ExpectClose(*file);
    ExpectUnmount();

    EXPECT_EQ(free - 3, FreeClusters());
    std::array<uint8_t, 1500> kept;
    std::copy(contents.begin(), contents.begin() + 1500, kept.begin());
    ExpectContentsOnFullVolume("/log.txt", kept);
}

TEST_F(FatLiteFileWithLargeClustersTest, truncate_within_the_last_cluster_keeps_its_clusters)
{
    auto free = FreeClusters();
    auto contents = Pattern<1000>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, infra::MakeRange(contents));
    file->Seek(500);
    ExpectTruncate(*file);
    ExpectClose(*file);
    ExpectUnmount();

    EXPECT_EQ(free - 1, FreeClusters());
    std::array<uint8_t, 500> kept;
    std::copy(contents.begin(), contents.begin() + 500, kept.begin());
    ExpectContentsOnFullVolume("/log.txt", kept);
}

TEST_F(FatLiteFileTest, truncate_to_zero_frees_every_cluster)
{
    auto free = FreeClusters();
    auto contents = Pattern<3000>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, infra::MakeRange(contents));
    ExpectSync(*file);
    file->Seek(0);
    ExpectTruncate(*file);
    EXPECT_EQ(0u, file->Size());
    ExpectWrite(*file, infra::MakeRange(contents));
    ExpectClose(*file);
    ExpectUnmount();

    EXPECT_EQ(free - 6, FreeClusters());
    ExpectContentsOnFullVolume("/log.txt", contents);
}

TEST_F(FatLiteFileTest, truncate_at_the_end_writes_nothing)
{
    auto contents = Pattern<700>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, infra::MakeRange(contents));
    ExpectSync(*file);

    auto writes = device.WriteRequests();
    ExpectTruncate(*file);
    EXPECT_EQ(writes, device.WriteRequests());
}

TEST_F(FatLiteFileTest, an_empty_file_that_holds_a_cluster_releases_it_when_truncated)
{
    fs::ShortEntry entry;
    entry.name = fs::ShortName{ { 'E', 'M', 'P', 'T', 'Y', ' ', ' ', ' ', 'B', 'I', 'N' } };
    entry.firstCluster = builder->AllocateChain(2);
    builder->AddEntries(0, "", entry);
    auto free = FreeClusters();
    ExpectMount();

    auto file = ExpectOpen("/empty.bin", fs::OpenMode::write);
    ExpectClose(*file);
    ExpectUnmount();
    EXPECT_EQ(free + 2, FreeClusters());
}

TEST_F(FatLiteFileTest, a_file_opened_for_reading_refuses_changes)
{
    auto contents = Pattern<100>();
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(contents));
    auto file = ExpectOpen("/log.txt", fs::OpenMode::read);

    ExpectWrite(*file, infra::MakeRange(contents), fs::Result::readOnly);
    ExpectTruncate(*file, fs::Result::readOnly);
    file->ExpectSize(1000, Done());
    ExpectDone(fs::Result::readOnly);
    ExpectSync(*file);
    ExpectClose(*file);
}

TEST_F(FatLiteFileTest, expect_size_is_unsupported)
{
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);

    file->ExpectSize(1000, Done());
    ExpectDone(fs::Result::unsupported);
    ExpectUnmount();
    file->ExpectSize(1000, Done());
    ExpectDone(fs::Result::notMounted);
}

TEST_F(FatLiteFileTest, a_call_while_another_is_pending_is_busy)
{
    auto contents = Pattern<100>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);

    file->Write(infra::MakeRange(contents), Done());
    file->Read(infra::MakeRange(buffer), [this](fs::Result result, std::size_t bytesRead)
        {
            readDone.callback(result, bytesRead);
        });
    file->Write(infra::MakeRange(contents), Done());
    file->Truncate(Done());
    file->ExpectSize(10, Done());
    file->Sync(Done());
    file->Close(Done());

    EXPECT_CALL(readDone, callback(fs::Result::busy, 0));
    EXPECT_CALL(done, callback(fs::Result::busy)).Times(5);
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatLiteFileTest, sync_commits_the_size_and_the_first_cluster)
{
    auto contents = Pattern<700>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    ExpectWrite(*file, infra::MakeRange(contents));
    ExpectSync(*file);

    ExpectContentsOnFullVolume("/log.txt", contents);
}

TEST_F(FatLiteFileTest, releasing_the_last_reference_commits_the_file)
{
    auto contents = Pattern<700>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    ExpectWrite(*file, infra::MakeRange(contents));
    file = nullptr;
    ExecuteAllActions();

    ExpectUnmount();
    ExpectContentsOnFullVolume("/log.txt", contents);
}

TEST_F(FatLiteFileTest, releasing_an_unchanged_file_writes_nothing)
{
    auto contents = Pattern<700>();
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(contents));
    auto file = ExpectOpen("/log.txt", fs::OpenMode::append);

    auto writes = device.WriteRequests();
    file = nullptr;
    ExecuteAllActions();
    EXPECT_EQ(writes, device.WriteRequests());
    ExpectOpen("/log.txt", fs::OpenMode::append);
}

TEST_F(FatLiteFileTest, calls_after_unmount_are_not_mounted)
{
    auto contents = Pattern<100>();
    ExpectMount();
    WriteFile("/read.txt", infra::MakeRange(contents));
    auto reader = ExpectOpen("/read.txt", fs::OpenMode::read);
    auto writer = ExpectOpen("/write.txt", fs::OpenMode::write);
    ExpectUnmount();

    ExpectRead(*writer, infra::MakeRange(buffer), fs::Result::notMounted, 0);
    ExpectWrite(*writer, infra::MakeRange(contents), fs::Result::notMounted);
    ExpectTruncate(*writer, fs::Result::notMounted);
    ExpectSync(*writer, fs::Result::notMounted);
    ExpectClose(*writer, fs::Result::notMounted);
    ExpectSync(*reader, fs::Result::notMounted);
    ExpectClose(*reader, fs::Result::notMounted);
}

TEST_F(FatLiteFileTest, a_failed_write_keeps_the_bytes_written_before_the_failure)
{
    auto contents = Pattern<2048>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);
    ExpectSync(*file);

    device.FailNextOperationWith(hal::BlockDevice::Result::failed, 2);
    ExpectWrite(*file, infra::MakeRange(contents), fs::Result::deviceError);
    EXPECT_EQ(512u, file->Size());
    EXPECT_EQ(512u, file->Position());
    ExpectClose(*file);
    ExpectUnmount();

    std::array<uint8_t, 512> written;
    std::copy(contents.begin(), contents.begin() + 512, written.begin());
    ExpectContentsOnFullVolume("/log.txt", written);
}

TEST_F(FatLiteFileTest, a_chain_shorter_than_its_file_is_corrupted)
{
    auto contents = Pattern<1000>();
    auto first = builder->AddFile(0, "", "SHORT   BIN", infra::MakeRange(contents));
    builder->SetFatEntry(first, fs::EndOfChain(builder->Geometry().type));
    ExpectMount();

    auto reader = ExpectOpen("/short.bin", fs::OpenMode::read);
    ExpectRead(*reader, infra::MakeRange(buffer), fs::Result::corrupted, 512);
    reader = nullptr;

    auto writer = ExpectOpen("/short.bin", fs::OpenMode::readWrite);
    writer->Seek(900);
    ExpectWrite(*writer, infra::MakeRange(contents), fs::Result::corrupted);
    writer->Seek(800);
    ExpectTruncate(*writer, fs::Result::corrupted);
}

TEST_F(FatLiteFileTest, a_file_of_4_gib_minus_one_byte_takes_no_more_data)
{
    fs::ShortEntry entry;
    entry.name = fs::ShortName{ { 'H', 'U', 'G', 'E', ' ', ' ', ' ', ' ', 'B', 'I', 'N' } };
    entry.firstCluster = builder->AllocateChain(1);
    entry.size = 0xffffffff;
    builder->AddEntries(0, "", entry);
    auto contents = Pattern<100>();
    ExpectMount();

    auto file = ExpectOpen("/huge.bin", fs::OpenMode::append);
    ExpectWrite(*file, infra::MakeRange(contents), fs::Result::noSpace);
    file->Seek(0xffffff00);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::corrupted, 0);
}

TEST_F(FatLiteFileTest, growing_into_the_next_fat_sector_flushes_before_the_link)
{
    for (uint32_t cluster = 2; cluster != 255; ++cluster)
        builder->SetFatEntry(cluster, fs::EndOfChain(builder->Geometry().type));
    auto contents = Pattern<1024>();
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::write);

    auto flushes = device.FlushRequests();
    ExpectWrite(*file, infra::MakeRange(contents));
    EXPECT_LT(flushes, device.FlushRequests());
    ExpectWrite(*file, infra::MakeRange(contents));
    ExpectClose(*file);
    ExpectUnmount();

    fs::FatImageBuilder image{ device };
    EXPECT_EQ(256u, image.FatEntry(255));
    EXPECT_EQ(257u, image.FatEntry(256));
}

TEST_F(FatLiteFileTest, the_allocator_wraps_around_to_the_start_of_the_volume)
{
    auto last = builder->Geometry().clusterCount + 1;
    UseEveryClusterFrom(3);
    builder->SetFatEntry(last, 0);
    auto contents = Pattern<1024>();
    ExpectMount();

    WriteFile("/log.txt", infra::MakeRange(contents));
    ExpectUnmount();

    fs::FatImageBuilder image{ device };
    EXPECT_EQ(last, image.FatEntry(2));
    EXPECT_TRUE(fs::IsEndOfChain(image.Geometry().type, image.FatEntry(last)));
}

TEST_F(FatLiteFileTest, every_copy_of_the_fat_holds_the_same_entries)
{
    auto contents = Pattern<5000>();
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(contents));
    ExpectUnmount();

    const auto& geometry = builder->Geometry();
    for (uint32_t sector = 0; sector != geometry.sectorsPerFat; ++sector)
        EXPECT_EQ(builder->ReadSector(geometry.firstFatSector + sector), builder->ReadSector(geometry.firstFatSector + geometry.sectorsPerFat + sector)) << sector;
}

TEST_F(FatLiteFileWithOneFatTest, a_volume_with_one_fat_is_written)
{
    auto contents = Pattern<5000>();
    ExpectMount();
    WriteFile("/log.txt", infra::MakeRange(contents));
    ExpectUnmount();

    ExpectContentsOnFullVolume("/log.txt", contents);
}

TEST_F(FatLiteFileWithTransferAlignmentTest, whole_sectors_go_directly_to_and_from_a_buffer_at_the_transfer_alignment)
{
    auto contents = Pattern<4096>();
    infra::Copy(infra::MakeRange(contents), At(fs::bufferAlignment, 4096));
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, At(fs::bufferAlignment, 4096));

    auto writes = device.WriteRequests();
    ExpectWrite(*file, At(fs::bufferAlignment, 4096));
    EXPECT_EQ(writes + 1, device.WriteRequests());

    file->Seek(0);
    auto reads = device.ReadRequests();
    ExpectRead(*file, At(fs::bufferAlignment, 4096), fs::Result::success, 4096);
    EXPECT_EQ(reads + 1, device.ReadRequests());
    EXPECT_TRUE(std::ranges::equal(contents, At(fs::bufferAlignment, 4096)));
    EXPECT_LE(fs::bufferAlignment, device.BufferAlignment());
}

TEST_F(FatLiteFileWithTransferAlignmentTest, whole_sectors_go_through_the_window_to_and_from_a_buffer_off_the_transfer_alignment)
{
    auto contents = Pattern<4096>();
    infra::Copy(infra::MakeRange(contents), At(4, 4096));
    ExpectMount();
    auto file = ExpectOpen("/log.txt", fs::OpenMode::readWrite);
    ExpectWrite(*file, At(4, 4096));

    std::ranges::fill(At(4, 4096), uint8_t{ 0 });
    file->Seek(0);
    ExpectRead(*file, At(4, 4096), fs::Result::success, 4096);
    EXPECT_TRUE(std::ranges::equal(contents, At(4, 4096)));
    EXPECT_LE(fs::bufferAlignment, device.BufferAlignment());

    ExpectClose(*file);
    ExpectUnmount();
    ExpectContentsOnFullVolume("/log.txt", contents);
}
