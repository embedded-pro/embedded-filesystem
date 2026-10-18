#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/reader/test/FatReaderFixture.hpp"
#include <algorithm>

namespace
{
    class FatReaderFileTest
        : public FatReaderEveryTypeTest
    {};

    class FatReaderFileFat16Test
        : public testing::Test
        , public FatReaderFixture
    {};

    class FatReaderFileWithTransferAlignmentTest
        : public testing::Test
        , public FatReaderFixture
    {
    public:
        FatReaderFileWithTransferAlignmentTest()
            : FatReaderFixture(fat12Volume, fs::bufferAlignment)
        {}

        infra::ByteRange At(std::size_t offset, std::size_t size)
        {
            return infra::Head(infra::DiscardHead(infra::MakeRange(aligned), offset), size);
        }

        alignas(fs::bufferAlignment) std::array<uint8_t, 4096 + fs::bufferAlignment> aligned{};
    };

    class FatReaderFileFat12Test
        : public testing::Test
        , public FatReaderFixture
    {
    public:
        FatReaderFileFat12Test()
            : FatReaderFixture(FatReaderVolumeCase{ "fat12", 4096, 1, true })
        {}
    };
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatReaderFileTest, testing::Values(fat12Volume, fat16Volume, fat32Volume));

TEST_P(FatReaderFileTest, a_file_is_read_across_clusters)
{
    auto contents = Pattern<5000>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/data.bin");
    EXPECT_EQ(5000u, file->Size());
    EXPECT_EQ(0u, file->Position());

    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 3000), fs::Result::success, 3000);
    ExpectRead(*file, infra::DiscardHead(infra::MakeRange(buffer), 3000), fs::Result::success, 2000);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);

    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
    EXPECT_EQ(5000u, file->Position());
}

TEST_P(FatReaderFileTest, seek_moves_the_position_of_the_next_read)
{
    auto contents = Pattern<5000>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/data.bin");

    file->Seek(4321);
    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 10), fs::Result::success, 10);
    EXPECT_TRUE(std::equal(contents.begin() + 4321, contents.begin() + 4331, buffer.begin()));

    file->Seek(17);
    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 600), fs::Result::success, 600);
    EXPECT_TRUE(std::equal(contents.begin() + 17, contents.begin() + 617, buffer.begin()));

    file->Seek(6000);
    EXPECT_EQ(5000u, file->Position());
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);
}

TEST_F(FatReaderFileFat16Test, whole_sectors_are_read_into_the_buffer_in_one_request_per_cluster)
{
    auto contents = Pattern<2048>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/data.bin");
    auto reads = device.ReadRequests();

    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 2048), fs::Result::success, 2048);

    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
    EXPECT_EQ(reads + 4 + 1, device.ReadRequests());
}

TEST_F(FatReaderFileFat16Test, an_empty_file_reads_nothing)
{
    builder->AddFile(0, "", "EMPTY   TXT", infra::ConstByteRange());
    ExpectMount();
    auto file = ExpectOpen("/empty.txt");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);
    EXPECT_EQ(0u, file->Size());
}

TEST_F(FatReaderFileFat16Test, a_second_read_on_a_handle_is_busy)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    StartRead(*file, infra::MakeRange(buffer));
    StartRead(*file, infra::MakeRange(buffer));

    EXPECT_CALL(readDone, callback(fs::Result::busy, 0));
    EXPECT_CALL(readDone, callback(fs::Result::success, 100));
    ExecuteAllActions();
}

TEST_F(FatReaderFileFat16Test, a_read_completes_when_the_last_reference_is_released_during_it)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    StartRead(*file, infra::MakeRange(buffer));
    file = nullptr;

    EXPECT_CALL(readDone, callback(fs::Result::success, 100));
    ExecuteAllActions();
    ExpectOpen("/file.txt");
    ExpectOpen("/file.txt");
}

TEST_F(FatReaderFileFat16Test, writing_a_file_is_read_only)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    file->Write(infra::Head(infra::MakeRange(buffer), 1), Done());
    file->Truncate(Done());
    file->ExpectSize(100, Done());
    EXPECT_CALL(done, callback(fs::Result::readOnly)).Times(3);
    ExecuteAllActions();

    file->Sync(Done());
    file->Close(Done());
    EXPECT_CALL(done, callback(fs::Result::success)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatReaderFileFat16Test, handles_are_not_mounted_after_unmount_also_after_a_new_mount)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");
    ExpectUnmount();

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::notMounted, 0);
    file->Write(infra::Head(infra::MakeRange(buffer), 1), Done());
    file->Sync(Done());
    EXPECT_CALL(done, callback(fs::Result::notMounted)).Times(2);
    ExecuteAllActions();

    ExpectMount();
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::notMounted, 0);
    ExpectStat("/file.txt", fs::Result::success, fs::EntryInfo{ "FILE.TXT", fs::EntryType::file, 100 });
}

TEST_F(FatReaderFileFat16Test, a_chain_shorter_than_the_size_is_corrupted)
{
    auto contents = Pattern<1500>();
    auto first = builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    builder->SetFatEntry(first, fs::EndOfChain(fs::FatType::fat16));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::corrupted, 512);
    EXPECT_EQ(512u, file->Position());
}

TEST_F(FatReaderFileFat16Test, a_chain_into_a_free_cluster_is_corrupted)
{
    auto contents = Pattern<1500>();
    auto first = builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    builder->SetFatEntry(first, 0);
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::corrupted, 512);
}

TEST_F(FatReaderFileFat16Test, a_file_larger_than_the_volume_is_corrupted_beyond_its_clusters)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'H', 'U', 'G', 'E', ' ', ' ', ' ', ' ', 'B', 'I', 'N' };
    entry.firstCluster = builder->AllocateChain(1);
    entry.size = 0xffffffff;
    builder->AddEntries(0, "", entry);
    ExpectMount();
    auto file = ExpectOpen("/huge.bin");

    file->Seek(builder->Geometry().clusterCount * 512);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::corrupted, 0);
}

TEST_F(FatReaderFileFat16Test, device_errors_while_reading_are_reported_with_the_bytes_read)
{
    auto contents = Pattern<1500>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);
    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 512), fs::Result::deviceError, 0);
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 1);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::deviceError, 512);
    device.FailNextOperationWith(hal::BlockDevice::Result::failed);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::deviceError, 0);

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 988);
    EXPECT_TRUE(std::equal(contents.begin() + 512, contents.end(), buffer.begin()));
}

TEST_F(FatReaderFileFat16Test, removed_card_while_reading_faults_the_volume)
{
    auto contents = Pattern<1500>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::mediaRemoved, 0);

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::mediaRemoved, 0);
    file->Close(Done());
    ExpectDone(fs::Result::mediaRemoved);
    ExpectStat("/file.txt", fs::Result::mediaRemoved);
}

TEST_F(FatReaderFileFat12Test, fat12_entries_that_straddle_two_sectors_are_followed)
{
    auto contents = Pattern<2048>();
    builder->AllocateChain(339);
    auto first = builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    EXPECT_EQ(341u, first);
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 2048);

    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
}

TEST_F(FatReaderFileWithTransferAlignmentTest, whole_sectors_go_directly_into_a_buffer_at_the_transfer_alignment)
{
    auto contents = Pattern<4096>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/data.bin");
    auto reads = device.ReadRequests();

    ExpectRead(*file, At(fs::bufferAlignment, 4096), fs::Result::success, 4096);

    EXPECT_EQ(reads + 1, device.ReadRequests());
    EXPECT_TRUE(std::ranges::equal(contents, At(fs::bufferAlignment, 4096)));
    EXPECT_LE(fs::bufferAlignment, device.BufferAlignment());
}

TEST_F(FatReaderFileWithTransferAlignmentTest, whole_sectors_go_through_the_window_into_a_buffer_off_the_transfer_alignment)
{
    auto contents = Pattern<4096>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/data.bin");
    auto reads = device.ReadRequests();

    ExpectRead(*file, At(4, 4096), fs::Result::success, 4096);

    EXPECT_EQ(reads + 8, device.ReadRequests());
    EXPECT_TRUE(std::ranges::equal(contents, At(4, 4096)));
    EXPECT_LE(fs::bufferAlignment, device.BufferAlignment());
}
