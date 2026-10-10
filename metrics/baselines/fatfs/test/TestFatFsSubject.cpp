#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/baselines/fatfs/DiskIo.hpp"
#include "metrics/baselines/fatfs/FatFsSubject.hpp"
#include "metrics/device/MeteredBlockDevice.hpp"
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

    class FatFsSubjectTest
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
            if (!file)
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
        metrics::FatFsSubject subject{ time, metered, testCard.blocksPerAllocationUnit };
        metrics::Runner runner{ time, subject, probe };
    };
}

TEST(FatFsResultTest, maps_fatfs_results)
{
    EXPECT_EQ(fs::Result::success, metrics::FatFsResult(FR_OK));
    EXPECT_EQ(fs::Result::deviceError, metrics::FatFsResult(FR_DISK_ERR));
    EXPECT_EQ(fs::Result::corrupted, metrics::FatFsResult(FR_INT_ERR));
    EXPECT_EQ(fs::Result::mediaRemoved, metrics::FatFsResult(FR_NOT_READY));
    EXPECT_EQ(fs::Result::notFound, metrics::FatFsResult(FR_NO_FILE));
    EXPECT_EQ(fs::Result::notFound, metrics::FatFsResult(FR_NO_PATH));
    EXPECT_EQ(fs::Result::invalidPath, metrics::FatFsResult(FR_INVALID_NAME));
    EXPECT_EQ(fs::Result::noSpace, metrics::FatFsResult(FR_DENIED));
    EXPECT_EQ(fs::Result::alreadyExists, metrics::FatFsResult(FR_EXIST));
    EXPECT_EQ(fs::Result::invalidPath, metrics::FatFsResult(FR_INVALID_OBJECT));
    EXPECT_EQ(fs::Result::readOnly, metrics::FatFsResult(FR_WRITE_PROTECTED));
    EXPECT_EQ(fs::Result::invalidPath, metrics::FatFsResult(FR_INVALID_DRIVE));
    EXPECT_EQ(fs::Result::notMounted, metrics::FatFsResult(FR_NOT_ENABLED));
    EXPECT_EQ(fs::Result::corrupted, metrics::FatFsResult(FR_NO_FILESYSTEM));
    EXPECT_EQ(fs::Result::deviceError, metrics::FatFsResult(FR_MKFS_ABORTED));
    EXPECT_EQ(fs::Result::busy, metrics::FatFsResult(FR_TIMEOUT));
    EXPECT_EQ(fs::Result::busy, metrics::FatFsResult(FR_LOCKED));
    EXPECT_EQ(fs::Result::tooManyOpenFiles, metrics::FatFsResult(FR_NOT_ENOUGH_CORE));
    EXPECT_EQ(fs::Result::tooManyOpenFiles, metrics::FatFsResult(FR_TOO_MANY_OPEN_FILES));
    EXPECT_EQ(fs::Result::invalidPath, metrics::FatFsResult(FR_INVALID_PARAMETER));
}

TEST_F(FatFsSubjectTest, is_named_after_its_variant)
{
    EXPECT_EQ(metrics::FatFsSubject::tuned ? "fatfs/tuned" : "fatfs/default", subject.Name());
}

TEST_F(FatFsSubjectTest, ram_counts_the_volume_one_open_file_and_the_long_name_buffer)
{
    std::size_t linkMap = metrics::FatFsSubject::tuned ? metrics::FatFsSubject::linkMapEntries * sizeof(DWORD) : 0;

    EXPECT_EQ(sizeof(FATFS) + sizeof(FIL) + linkMap + (FF_MAX_LFN + 1) * sizeof(WCHAR), subject.RamBytes());
}

TEST_F(FatFsSubjectTest, the_subject_binds_the_disk)
{
    EXPECT_NE(nullptr, metrics::DiskIo().device);
}

TEST_F(FatFsSubjectTest, a_second_subject_at_the_same_time_aborts)
{
    EXPECT_DEATH((metrics::FatFsSubject{ time, metered, testCard.blocksPerAllocationUnit }), "");
}

TEST(FatFsBindingTest, a_destroyed_subject_unbinds_the_disk)
{
    infra::ClockFixture clock;
    metrics::VirtualTime time{ clock.systemTimerService };
    std::vector<uint8_t> storage(16384 * 512);
    hal::BlockDeviceStub stub{ infra::MakeRange(storage), 512 };
    metrics::MeteredBlockDevice metered{ stub, testCard };

    {
        metrics::FatFsSubject subject{ time, metered, testCard.blocksPerAllocationUnit };
    }

    EXPECT_EQ(nullptr, metrics::DiskIo().device);
}

TEST_F(FatFsSubjectTest, the_tuned_variant_reports_the_allocation_unit_as_erase_block)
{
    EXPECT_EQ(metrics::FatFsSubject::tuned ? 1024 : 0, metrics::DiskIo().eraseBlockSectors);
}

TEST_F(FatFsSubjectTest, mount_of_an_unformatted_device_reports_corruption)
{
    EXPECT_EQ(fs::Result::corrupted, runner.Mount());
}

TEST_F(FatFsSubjectTest, format_and_mount_use_the_metered_device)
{
    FormatAndMount();

    EXPECT_LT(0, metered.Counters().writeRequests);
    EXPECT_LT(0, runner.Records().front().latency.count());
}

TEST_F(FatFsSubjectTest, a_written_file_reads_back)
{
    FormatAndMount();
    auto content = Pattern(10000, 1);

    WriteFile("/a-long-file-name.bin", content);

    EXPECT_EQ(content, ReadFile("/a-long-file-name.bin"));
}

TEST_F(FatFsSubjectTest, files_survive_a_remount)
{
    FormatAndMount();
    auto content = Pattern(5000, 2);
    WriteFile("/file", content);

    ASSERT_EQ(fs::Result::success, runner.Unmount());
    ASSERT_EQ(fs::Result::success, runner.Mount());

    EXPECT_EQ(content, ReadFile("/file"));
}

TEST_F(FatFsSubjectTest, opening_a_missing_file_for_reading_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.Open("/missing", metrics::OpenMode::read).has_value());
    EXPECT_EQ(fs::Result::notFound, runner.Records().back().result);
}

TEST_F(FatFsSubjectTest, append_continues_at_the_end_and_sync_flushes_the_device)
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

TEST_F(FatFsSubjectTest, seek_then_write_overwrites_in_place)
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

TEST_F(FatFsSubjectTest, remove_deletes_the_file)
{
    FormatAndMount();
    WriteFile("/file", Pattern(10, 4));

    EXPECT_EQ(fs::Result::success, runner.Remove("/file"));

    EXPECT_FALSE(runner.Open("/file", metrics::OpenMode::read).has_value());
}

TEST_F(FatFsSubjectTest, removing_a_file_discards_its_clusters_only_when_tuned)
{
    FormatAndMount();
    WriteFile("/file", Pattern(100000, 4));
    auto discards = metered.Counters().discardRequests;

    runner.Remove("/file");

    EXPECT_EQ(metrics::FatFsSubject::tuned, metered.Counters().discardRequests > discards);
}

TEST_F(FatFsSubjectTest, list_returns_the_files_and_directories)
{
    FormatAndMount();
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory"));
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory/inner"));
    WriteFile("/directory/file", Pattern(7, 5));

    auto entries = runner.List("/directory");

    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ((std::vector<metrics::DirectoryEntry>{ { "inner", true, 0 }, { "file", false, 7 } }), *entries);
}

TEST_F(FatFsSubjectTest, list_of_a_missing_directory_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.List("/missing").has_value());
}

TEST_F(FatFsSubjectTest, making_a_directory_twice_reports_that_it_exists)
{
    FormatAndMount();
    runner.MakeDirectory("/directory");

    EXPECT_EQ(fs::Result::alreadyExists, runner.MakeDirectory("/directory"));
}

TEST_F(FatFsSubjectTest, expect_size_preallocates_only_when_tuned)
{
    FormatAndMount();
    auto file = runner.Open("/file", metrics::OpenMode::write);
    ASSERT_TRUE(file.has_value());

    auto result = runner.ExpectSize(*file, 65536);

    EXPECT_EQ(metrics::FatFsSubject::tuned ? fs::Result::success : fs::Result::unsupported, result);
    EXPECT_EQ(metrics::FatFsSubject::tuned ? 65536 : 0, runner.Size(*file));
    runner.Close(*file);
}

TEST_F(FatFsSubjectTest, a_preallocated_file_is_written_from_its_start)
{
    FormatAndMount();
    auto content = Pattern(8192, 6);
    auto file = runner.Open("/file", metrics::OpenMode::write);
    ASSERT_TRUE(file.has_value());

    runner.ExpectSize(*file, 8192);
    runner.Write(*file, infra::MakeRange(content));
    runner.Close(*file);

    EXPECT_EQ(content, ReadFile("/file"));
}

TEST_F(FatFsSubjectTest, seeking_a_file_opened_for_reading_reads_only_the_data_sector_when_tuned)
{
    FormatAndMount();
    WriteFile("/file", Pattern(200000, 7));
    auto file = runner.Open("/file", metrics::OpenMode::read);
    ASSERT_TRUE(file.has_value());

    runner.Seek(*file, 150000);

    if (metrics::FatFsSubject::tuned)
        EXPECT_EQ(1, runner.Records().back().device.readRequests);
    runner.Close(*file);
}

TEST_F(FatFsSubjectTest, random_reads_through_the_link_map_return_the_contents)
{
    FormatAndMount();
    auto content = Pattern(200000, 8);
    WriteFile("/file", content);
    auto file = runner.Open("/file", metrics::OpenMode::read);
    ASSERT_TRUE(file.has_value());
    std::vector<uint8_t> data(512);

    runner.Seek(*file, 150016);
    runner.Read(*file, infra::MakeRange(data));

    EXPECT_TRUE(metrics::MatchesPattern(infra::MakeRange(data), 8, 150016));
    runner.Close(*file);
}

TEST_F(FatFsSubjectTest, opening_more_files_than_the_handles_reports_too_many_open_files)
{
    FormatAndMount();
    for (std::size_t index = 0; index != metrics::FatFsSubject::maxOpenFiles; ++index)
        ASSERT_TRUE(runner.Open("/file" + std::to_string(index), metrics::OpenMode::write).has_value());

    EXPECT_FALSE(runner.Open("/one-more", metrics::OpenMode::write).has_value());
    EXPECT_EQ(fs::Result::tooManyOpenFiles, runner.Records().back().result);
}

TEST_F(FatFsSubjectTest, writing_beyond_the_capacity_reports_no_space)
{
    FormatAndMount();
    auto file = runner.Open("/large", metrics::OpenMode::write);
    ASSERT_TRUE(file.has_value());
    std::vector<uint8_t> chunk(512 * 1024, 0x12);

    auto result = fs::Result::success;
    for (int written = 0; written != 20 && result == fs::Result::success; ++written)
        result = runner.Write(*file, infra::MakeRange(chunk));

    EXPECT_EQ(fs::Result::noSpace, result);
}

TEST_F(FatFsSubjectTest, idle_completes_and_lasts_the_idle_period)
{
    FormatAndMount();
    auto start = time.Now();

    runner.Idle(100ms);

    EXPECT_EQ(start + 100ms, time.Now());
}
