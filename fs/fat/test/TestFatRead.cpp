#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>

namespace
{
    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern()
    {
        std::array<uint8_t, Size> pattern;

        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 31 + index / 509);

        return pattern;
    }

    class FatReadTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatReadTest(uint32_t blocks = 16384, const fs::FatConfig& volumeConfig = {})
            : FatFileSystemFixture(blocks, ClusterSize(1), volumeConfig)
        {}

        void SetUp() override
        {
            ExpectMount();
        }

        void AddSpreadFile(const char (&shortName)[12], uint32_t clusters)
        {
            const uint32_t stride = 300;

            for (uint32_t index = 0; index != clusters; ++index)
                builder->SetFatEntry(2 + index * stride, index + 1 == clusters ? fs::EndOfChain(builder->Geometry().type) : 2 + (index + 1) * stride);

            fs::ShortEntry entry;
            std::copy_n(shortName, 11, entry.name.bytes.begin());
            entry.attributes = fs::FatAttributes::archive;
            entry.firstCluster = 2;
            entry.size = clusters * 512;
            builder->AddEntries(0, "", entry);
        }

        uint32_t RequestsToRead(fs::File& file, uint32_t position, std::size_t bytes)
        {
            file.Seek(position);
            auto before = device.ReadRequests();
            ExpectRead(file, infra::Head(infra::MakeRange(buffer), bytes), fs::Result::success, bytes);
            return device.ReadRequests() - before;
        }

        void ReadSectorsOneByOne(fs::File& file, uint32_t sectors)
        {
            for (uint32_t sector = 0; sector != sectors; ++sector)
                ExpectRead(file, infra::Head(infra::MakeRange(buffer), 512), fs::Result::success, 512);
        }

        std::array<uint8_t, 8192> buffer{};
    };

    class FatReadWithTransferAlignmentTest
        : public FatReadTest
    {
    public:
        FatReadWithTransferAlignmentTest()
            : FatReadTest(16384, fs::FatConfig{ .transferAlignment = fs::bufferAlignment })
        {}

        infra::ByteRange At(std::size_t offset, std::size_t size)
        {
            return infra::Head(infra::DiscardHead(infra::MakeRange(aligned), offset), size);
        }

        alignas(fs::bufferAlignment) std::array<uint8_t, 2048 + fs::bufferAlignment> aligned{};
    };

    class FatReadOnFat32Test
        : public FatReadTest
    {
    public:
        FatReadOnFat32Test()
            : FatReadTest(81920)
        {}

        void SetUp() override
        {}
    };
}

TEST_F(FatReadTest, reads_a_file_smaller_than_a_sector)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "SMALL   TXT", infra::MakeRange(contents));
    auto file = ExpectOpen("/small.txt");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 100);

    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
    EXPECT_EQ(100u, file->Position());
}

TEST_F(FatReadTest, reads_from_any_position_across_sectors_and_clusters)
{
    auto contents = Pattern<2560>();
    builder->AddFile(0, "", "SPREAD  TXT", infra::MakeRange(contents), true);
    auto file = ExpectOpen("/spread.txt");
    file->Seek(100);

    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 2000), fs::Result::success, 2000);

    EXPECT_TRUE(std::equal(contents.begin() + 100, contents.begin() + 2100, buffer.begin()));
    EXPECT_EQ(2100u, file->Position());
}

TEST_F(FatReadTest, consecutive_reads_continue_where_the_last_one_ended)
{
    auto contents = Pattern<1500>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents), true);
    auto file = ExpectOpen("/file.txt");

    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 700), fs::Result::success, 700);
    ExpectRead(*file, infra::DiscardHead(infra::Head(infra::MakeRange(buffer), 1400), 700), fs::Result::success, 700);

    EXPECT_TRUE(std::equal(contents.begin(), contents.begin() + 1400, buffer.begin()));
}

TEST_F(FatReadTest, read_stops_at_the_end_of_the_file)
{
    auto contents = Pattern<1000>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    auto file = ExpectOpen("/file.txt");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 1000);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);

    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
}

TEST_F(FatReadTest, seek_beyond_the_end_moves_to_the_end)
{
    auto contents = Pattern<1000>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    auto file = ExpectOpen("/file.txt");

    file->Seek(5000);

    EXPECT_EQ(1000u, file->Position());
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);
}

TEST_F(FatReadTest, empty_file_reads_nothing)
{
    builder->AddFile(0, "", "EMPTY   TXT", infra::ConstByteRange());
    auto file = ExpectOpen("/empty.txt");
    auto before = device.ReadRequests();

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);

    EXPECT_EQ(0u, file->Size());
    EXPECT_EQ(before, device.ReadRequests());
}

TEST_F(FatReadTest, whole_sectors_of_contiguous_clusters_are_read_in_one_request)
{
    auto contents = Pattern<8192>();
    builder->AddFile(0, "", "LARGE   BIN", infra::MakeRange(contents));
    auto file = ExpectOpen("/large.bin");

    EXPECT_EQ(2u, RequestsToRead(*file, 0, 8192));
    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));

    EXPECT_EQ(1u, RequestsToRead(*file, 0, 8192));
}

TEST_F(FatReadTest, fragmented_file_is_read_in_one_request_per_run)
{
    auto contents = Pattern<2048>();
    builder->AddFile(0, "", "FRAGMENTBIN", infra::MakeRange(contents), true);
    auto file = ExpectOpen("/fragment.bin");

    EXPECT_EQ(5u, RequestsToRead(*file, 0, 2048));
    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
}

TEST_F(FatReadTest, seek_within_a_cached_run_reads_no_fat)
{
    AddSpreadFile("SPREAD  BIN", 8);
    auto file = ExpectOpen("/spread.bin");
    ReadSectorsOneByOne(*file, 8);

    EXPECT_EQ(1u, RequestsToRead(*file, 7 * 512, 512));
    EXPECT_EQ(1u, RequestsToRead(*file, 0, 512));
}

TEST_F(FatReadTest, seek_outside_the_cached_runs_walks_from_the_nearest_run_before)
{
    AddSpreadFile("SPREAD  BIN", 10);
    auto file = ExpectOpen("/spread.bin");
    ReadSectorsOneByOne(*file, 6);

    EXPECT_EQ(4u, RequestsToRead(*file, 8 * 512, 512));
}

TEST_F(FatReadTest, reopened_file_starts_with_its_last_run)
{
    AddSpreadFile("SPREAD  BIN", 8);
    auto file = ExpectOpen("/spread.bin");
    ReadSectorsOneByOne(*file, 8);
    file->Close([this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::success);
    file = nullptr;

    file = ExpectOpen("/spread.bin");

    EXPECT_EQ(1u, RequestsToRead(*file, 7 * 512, 512));
}

TEST_F(FatReadTest, file_released_without_close_is_remembered_too)
{
    AddSpreadFile("SPREAD  BIN", 8);
    auto file = ExpectOpen("/spread.bin");
    ReadSectorsOneByOne(*file, 8);
    file = nullptr;

    file = ExpectOpen("/spread.bin");

    EXPECT_EQ(1u, RequestsToRead(*file, 7 * 512, 512));
}

TEST_F(FatReadTest, files_closed_before_a_new_mount_are_forgotten)
{
    AddSpreadFile("SPREAD  BIN", 8);
    auto file = ExpectOpen("/spread.bin");
    ReadSectorsOneByOne(*file, 8);
    file = nullptr;
    ExpectUnmount();
    ExpectMount();

    file = ExpectOpen("/spread.bin");

    EXPECT_LT(1u, RequestsToRead(*file, 7 * 512, 512));
}

TEST_F(FatReadTest, file_from_an_earlier_mount_is_not_remembered)
{
    AddSpreadFile("SPREAD  BIN", 8);
    auto file = ExpectOpen("/spread.bin");
    ReadSectorsOneByOne(*file, 8);
    ExpectUnmount();
    auto contents = Pattern<512>();
    builder->SetFatEntry(2 + 6 * 300, builder->AddFile(0, "", "MOVED   BIN", infra::MakeRange(contents)));
    ExpectMount();
    file = nullptr;

    file = ExpectOpen("/spread.bin");
    file->Seek(7 * 512);

    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 512), fs::Result::success, 512);
    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
}

TEST_F(FatReadTest, chain_shorter_than_the_file_is_corrupted_after_the_bytes_before_it)
{
    auto contents = Pattern<1024>();
    builder->AddFile(0, "", "SHORT   BIN", infra::MakeRange(contents));
    fs::ShortEntry entry;
    entry.name.bytes = { 'L', 'O', 'N', 'G', 'E', 'R', ' ', ' ', 'B', 'I', 'N' };
    entry.attributes = fs::FatAttributes::archive;
    entry.firstCluster = builder->AddFile(0, "", "OTHER   BIN", infra::MakeRange(contents));
    entry.size = 1536;
    builder->AddEntries(0, "", entry);
    auto file = ExpectOpen("/longer.bin");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::corrupted, 1024);

    EXPECT_EQ(1024u, file->Position());
}

TEST_F(FatReadTest, device_error_reports_the_bytes_read_before_it)
{
    auto contents = Pattern<1024>();
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents), true);
    auto file = ExpectOpen("/file.bin");
    file->Seek(100);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 2);

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::deviceError, 412);

    EXPECT_EQ(512u, file->Position());
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 512);
    EXPECT_TRUE(std::equal(contents.begin() + 512, contents.end(), buffer.begin()));
}

TEST_F(FatReadOnFat32Test, reads_a_file_in_the_root_of_fat32)
{
    auto contents = Pattern<1300>();
    builder->AddFile(0, "in the root.bin", "INTHER~1BIN", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/in the root.bin");

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 1300);

    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
}

TEST_F(FatReadWithTransferAlignmentTest, whole_sectors_go_directly_into_a_buffer_at_the_transfer_alignment)
{
    auto contents = Pattern<2048>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents));
    auto file = ExpectOpen("/data.bin");
    auto reads = device.ReadRequests();

    ExpectRead(*file, At(fs::bufferAlignment, 2048), fs::Result::success, 2048);

    EXPECT_EQ(reads + 2, device.ReadRequests());
    EXPECT_TRUE(std::ranges::equal(contents, At(fs::bufferAlignment, 2048)));
    EXPECT_LE(fs::bufferAlignment, device.BufferAlignment());
}

TEST_F(FatReadWithTransferAlignmentTest, whole_sectors_go_through_the_cache_into_a_buffer_off_the_transfer_alignment)
{
    auto contents = Pattern<2048>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents));
    auto file = ExpectOpen("/data.bin");
    auto reads = device.ReadRequests();

    ExpectRead(*file, At(4, 2048), fs::Result::success, 2048);

    EXPECT_EQ(reads + 1 + 4, device.ReadRequests());
    EXPECT_TRUE(std::ranges::equal(contents, At(4, 2048)));
    EXPECT_LE(fs::bufferAlignment, device.BufferAlignment());
}
