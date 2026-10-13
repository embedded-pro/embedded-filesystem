#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/device/MeteredBlockDevice.hpp"
#include "metrics/fat/FatSubject.hpp"
#include "metrics/workload/Pattern.hpp"
#include "metrics/workload/Runner.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::BlockDeviceModel testCard{ "test-card", 512, 16384, 1024, 2, { 100us, 25us, 50us, 0us, 10ms }, { 100us, 25us, 50us, 250ms, 10ms } };

    std::vector<uint8_t> Pattern(std::size_t size, uint32_t fileId)
    {
        std::vector<uint8_t> data(size);
        metrics::FillPattern(infra::MakeRange(data), fileId, 0);
        return data;
    }

    class FatSubjectTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        void FormatAndMount()
        {
            ASSERT_EQ(fs::Result::success, runner.Format());
            ASSERT_EQ(fs::Result::success, runner.Mount());
        }

        void WriteFile(std::string_view path, const std::vector<uint8_t>& content)
        {
            auto file = runner.Open(path, metrics::OpenMode::write);
            ASSERT_TRUE(file.has_value());
            ASSERT_EQ(fs::Result::success, runner.Write(*file, infra::MakeRange(content)));
            ASSERT_EQ(fs::Result::success, runner.Close(*file));
        }

        std::vector<uint8_t> ReadFile(std::string_view path)
        {
            auto file = runner.Open(path, metrics::OpenMode::read);
            if (!file.has_value())
                return {};

            std::vector<uint8_t> content(runner.Size(*file));
            runner.Read(*file, infra::MakeRange(content));
            runner.Close(*file);
            return content;
        }

        metrics::VirtualTime time{ systemTimerService };
        std::vector<uint8_t> storage = std::vector<uint8_t>(16384 * 512, 0xff);
        hal::BlockDeviceStub stub{ infra::MakeRange(storage), 512 };
        metrics::MeteredBlockDevice metered{ stub, testCard };
        metrics::MeteredBlockDeviceProbe probe{ metered };
        metrics::FatSubject subject{ metered, testCard };
        metrics::Runner runner{ time, subject, probe };
    };
}

TEST_F(FatSubjectTest, is_named_after_the_fat_filesystem)
{
    EXPECT_EQ("embedded-filesystem/fat", subject.Name());
}

TEST_F(FatSubjectTest, ram_is_that_of_a_volume_with_one_open_file)
{
    EXPECT_EQ(sizeof(fs::FatFileSystem::WithStorage<1, 8>), subject.RamBytes());
}

TEST_F(FatSubjectTest, mount_of_an_unformatted_device_reports_corruption)
{
    EXPECT_EQ(fs::Result::corrupted, runner.Mount());
}

TEST_F(FatSubjectTest, format_and_mount_use_the_metered_device)
{
    FormatAndMount();

    EXPECT_LT(0, metered.Counters().writeRequests);
    EXPECT_LT(0, runner.Records().front().latency.count());
}

TEST_F(FatSubjectTest, a_written_file_reads_back)
{
    FormatAndMount();
    auto content = Pattern(10000, 1);

    WriteFile("/a-long-file-name.bin", content);

    EXPECT_EQ(content, ReadFile("/a-long-file-name.bin"));
}

TEST_F(FatSubjectTest, files_survive_a_remount)
{
    FormatAndMount();
    auto content = Pattern(5000, 2);
    WriteFile("/file", content);

    ASSERT_EQ(fs::Result::success, runner.Unmount());
    ASSERT_EQ(fs::Result::success, runner.Mount());

    EXPECT_EQ(content, ReadFile("/file"));
}

TEST_F(FatSubjectTest, opening_a_missing_file_for_reading_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.Open("/missing", metrics::OpenMode::read).has_value());
    EXPECT_EQ(fs::Result::notFound, runner.Records().back().result);
}

TEST_F(FatSubjectTest, append_continues_at_the_end_and_sync_flushes_the_device)
{
    FormatAndMount();
    WriteFile("/log", Pattern(100, 3));
    auto flushes = metered.Counters().flushRequests;

    auto file = runner.Open("/log", metrics::OpenMode::append);
    ASSERT_TRUE(file.has_value());
    std::vector<uint8_t> more(50, 0x11);
    runner.Write(*file, infra::MakeRange(more));

    EXPECT_EQ(fs::Result::success, runner.Sync(*file));
    EXPECT_EQ(150, runner.Size(*file));
    EXPECT_LT(flushes, metered.Counters().flushRequests);
    runner.Close(*file);
}

TEST_F(FatSubjectTest, seek_then_write_overwrites_in_place)
{
    FormatAndMount();
    WriteFile("/file", std::vector<uint8_t>(1024, 0x00));

    auto file = runner.Open("/file", metrics::OpenMode::readWrite);
    ASSERT_TRUE(file.has_value());
    std::vector<uint8_t> patch(4, 0x77);
    runner.Seek(*file, 512);
    runner.Write(*file, infra::MakeRange(patch));
    runner.Close(*file);

    auto content = ReadFile("/file");
    ASSERT_EQ(1024, content.size());
    EXPECT_EQ(0x00, content[511]);
    EXPECT_EQ(0x77, content[512]);
    EXPECT_EQ(0x77, content[515]);
    EXPECT_EQ(0x00, content[516]);
}

TEST_F(FatSubjectTest, remove_deletes_the_file_and_discards_its_clusters)
{
    FormatAndMount();
    WriteFile("/file", Pattern(100000, 4));
    auto discards = metered.Counters().discardRequests;

    EXPECT_EQ(fs::Result::success, runner.Remove("/file"));

    EXPECT_LT(discards, metered.Counters().discardRequests);
    EXPECT_FALSE(runner.Open("/file", metrics::OpenMode::read).has_value());
}

TEST_F(FatSubjectTest, list_returns_the_files_and_directories)
{
    FormatAndMount();
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory"));
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory/inner"));
    WriteFile("/directory/file", Pattern(7, 5));

    auto entries = runner.List("/directory");

    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ((std::vector<metrics::DirectoryEntry>{ { "inner", true, 0 }, { "file", false, 7 } }), *entries);
}

TEST_F(FatSubjectTest, list_of_a_missing_directory_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.List("/missing").has_value());
    EXPECT_EQ(fs::Result::notFound, runner.Records().back().result);
}

TEST_F(FatSubjectTest, making_a_directory_twice_reports_that_it_exists)
{
    FormatAndMount();
    runner.MakeDirectory("/directory");

    EXPECT_EQ(fs::Result::alreadyExists, runner.MakeDirectory("/directory"));
}

TEST_F(FatSubjectTest, a_file_written_after_expect_size_reads_back)
{
    FormatAndMount();
    auto content = Pattern(8192, 6);
    auto file = runner.Open("/file", metrics::OpenMode::write);
    ASSERT_TRUE(file.has_value());

    EXPECT_EQ(fs::Result::success, runner.ExpectSize(*file, 8192));
    runner.Write(*file, infra::MakeRange(content));
    runner.Close(*file);

    EXPECT_EQ(content, ReadFile("/file"));
}

TEST_F(FatSubjectTest, opening_more_files_than_the_handles_reports_too_many_open_files)
{
    FormatAndMount();
    ASSERT_TRUE(runner.Open("/file0", metrics::OpenMode::write).has_value());
    ASSERT_TRUE(runner.Open("/file1", metrics::OpenMode::write).has_value());

    EXPECT_FALSE(runner.Open("/one-more", metrics::OpenMode::write).has_value());
    EXPECT_EQ(fs::Result::tooManyOpenFiles, runner.Records().back().result);
}

TEST_F(FatSubjectTest, usage_grows_with_the_files_written)
{
    FormatAndMount();
    auto before = runner.QueryUsage();
    ASSERT_TRUE(before.has_value());

    WriteFile("/file", Pattern(100000, 9));

    auto after = runner.QueryUsage();
    ASSERT_TRUE(after.has_value());
    EXPECT_LE(before->usedBytes + 100000, after->usedBytes);
    EXPECT_EQ(before->totalBytes, after->totalBytes);
}

TEST_F(FatSubjectTest, idle_completes_and_lasts_the_idle_period)
{
    FormatAndMount();
    auto start = time.Now();

    runner.Idle(100ms);

    EXPECT_EQ(start + 100ms, time.Now());
}

TEST_F(FatSubjectTest, data_area_is_aligned_to_the_allocation_unit_of_the_model)
{
    FormatAndMount();

    auto partition = fs::FirstFatPartition(infra::Head(infra::MakeRange(storage), 512));
    ASSERT_TRUE(partition.has_value());
    auto parameters = fs::ParseBootSector(infra::Head(infra::DiscardHead(infra::MakeRange(storage), partition->firstSector * 512), 512));
    ASSERT_TRUE(parameters.has_value());
    auto geometry = fs::GeometryOf(*parameters, partition->firstSector, partition->sectors, 512);
    ASSERT_EQ(fs::Result::success, geometry.result);
    EXPECT_EQ(0u, geometry.geometry.firstDataSector % testCard.blocksPerAllocationUnit);
}
