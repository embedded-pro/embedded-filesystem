#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include "metrics/nor/NorSubject.hpp"
#include "metrics/workload/DeviceProbe.hpp"
#include "metrics/workload/Pattern.hpp"
#include "metrics/workload/Runner.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::NorModel testNor{ "test-nor", 64, 4096, 256, 1, { 400us, 45ms, 50'000'000 }, { 3ms, 400ms, 50'000'000 } };
    constexpr fs::NorConfig testConfig{ .maxBlocks = 64 };
    constexpr fs::NorConfig levelingConfig{ .maxBlocks = 64, .staticWearLevelingThreshold = 1, .staticWearLevelingInterval = 1 };

    std::vector<uint8_t> Pattern(std::size_t size, uint32_t fileId)
    {
        std::vector<uint8_t> data(size);
        metrics::FillPattern(infra::MakeRange(data), fileId, 0);
        return data;
    }

    class NorSubjectTest
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
        hal::FlashStub stub{ testNor.numberOfSectors, testNor.sectorSize };
        metrics::MeteredFlash metered{ stub, testNor };
        metrics::MeteredFlashProbe probe{ metered };
        metrics::NorSubject::WithConfig<testConfig> subject{ metered };
        metrics::Runner runner{ time, subject, probe };
    };
}

TEST_F(NorSubjectTest, is_named_after_the_nor_filesystem)
{
    EXPECT_EQ("embedded-filesystem/nor", subject.Name());
}

TEST_F(NorSubjectTest, ram_is_that_of_a_volume_with_one_open_file)
{
    EXPECT_EQ((sizeof(fs::NorFileSystem::WithStorage<testConfig, 1>)), subject.RamBytes());
}

TEST_F(NorSubjectTest, counts_the_sectors_of_the_blocks_it_allocates_and_its_checkpoints)
{
    FormatAndMount();
    WriteFile("/file", Pattern(10000, 1));
    ASSERT_EQ(fs::Result::success, runner.Unmount());

    auto counters = subject.Counters();
    ASSERT_TRUE(counters.has_value());
    EXPECT_LE(2, counters->allocatedSectors);
    EXPECT_LE(1, counters->checkpoints);
}

TEST_F(NorSubjectTest, counters_keep_growing_across_mounts)
{
    FormatAndMount();
    WriteFile("/file", Pattern(10000, 1));
    ASSERT_EQ(fs::Result::success, runner.Unmount());
    auto before = subject.Counters().value_or(metrics::VolumeCounters{});

    ASSERT_EQ(fs::Result::success, runner.Mount());
    WriteFile("/other", Pattern(10000, 2));

    auto after = subject.Counters().value_or(metrics::VolumeCounters{});
    EXPECT_LT(before.allocatedSectors, after.allocatedSectors);
    EXPECT_LE(before.checkpoints, after.checkpoints);
}

TEST_F(NorSubjectTest, a_failed_mount_keeps_the_counts_of_the_earlier_mounts)
{
    FormatAndMount();
    WriteFile("/file", Pattern(10000, 1));
    ASSERT_EQ(fs::Result::success, runner.Unmount());
    auto before = subject.Counters().value_or(metrics::VolumeCounters{});
    for (auto& sector : stub.sectors)
        std::ranges::fill(sector, uint8_t{ 0xff });

    ASSERT_EQ(fs::Result::corrupted, runner.Mount());

    auto after = subject.Counters().value_or(metrics::VolumeCounters{});
    EXPECT_LE(before.allocatedSectors, after.allocatedSectors);
    EXPECT_LE(before.checkpoints, after.checkpoints);
}

TEST_F(NorSubjectTest, counts_the_blocks_that_static_wear_leveling_moves_and_the_blocks_erased)
{
    metrics::NorSubject::WithConfig<levelingConfig> leveling{ metered };
    metrics::Runner levelingRunner{ time, leveling, probe };
    ASSERT_EQ(fs::Result::success, levelingRunner.Format());
    ASSERT_EQ(fs::Result::success, levelingRunner.Mount());

    auto still = Pattern(20000, 1);
    auto file = levelingRunner.Open("/static", metrics::OpenMode::write);
    ASSERT_TRUE(file.has_value());
    ASSERT_EQ(fs::Result::success, levelingRunner.Write(*file, infra::MakeRange(still)));
    ASSERT_EQ(fs::Result::success, levelingRunner.Close(*file));
    auto hot = Pattern(6000, 2);
    for (int rewrite = 0; rewrite != 100; ++rewrite)
    {
        auto rewritten = levelingRunner.Open("/hot", metrics::OpenMode::write);
        ASSERT_TRUE(rewritten.has_value());
        ASSERT_EQ(fs::Result::success, levelingRunner.Write(*rewritten, infra::MakeRange(hot)));
        ASSERT_EQ(fs::Result::success, levelingRunner.Close(*rewritten));
        levelingRunner.Idle(std::chrono::seconds(1));
    }

    auto counters = leveling.Counters().value_or(metrics::VolumeCounters{});
    EXPECT_LT(0, counters.movedBlocks);
    EXPECT_LT(2 * counters.movedBlocks, counters.erasedBlocks);
}

TEST_F(NorSubjectTest, mount_of_an_erased_device_reports_corruption)
{
    EXPECT_EQ(fs::Result::corrupted, runner.Mount());
}

TEST_F(NorSubjectTest, format_and_mount_use_the_metered_device)
{
    FormatAndMount();

    EXPECT_LT(0, metered.Counters().sectorsErased);
    EXPECT_LT(0, runner.Records().front().latency.count());
}

TEST_F(NorSubjectTest, a_written_file_reads_back_and_survives_a_remount)
{
    FormatAndMount();
    auto content = Pattern(10000, 1);
    WriteFile("/file", content);

    EXPECT_EQ(content, ReadFile("/file"));
    ASSERT_EQ(fs::Result::success, runner.Unmount());
    ASSERT_EQ(fs::Result::success, runner.Mount());
    EXPECT_EQ(content, ReadFile("/file"));
}

TEST_F(NorSubjectTest, opening_a_missing_file_for_reading_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.Open("/missing", metrics::OpenMode::read).has_value());
    EXPECT_EQ(fs::Result::notFound, runner.Records().back().result);
}

TEST_F(NorSubjectTest, append_continues_at_the_end_after_a_sync)
{
    FormatAndMount();
    WriteFile("/log", Pattern(100, 3));

    auto file = runner.Open("/log", metrics::OpenMode::append);
    ASSERT_TRUE(file.has_value());
    std::vector<uint8_t> more(50, 0x11);
    runner.Write(*file, infra::MakeRange(more));

    EXPECT_EQ(fs::Result::success, runner.Sync(*file));
    EXPECT_EQ(150, runner.Size(*file));
    runner.Close(*file);
}

TEST_F(NorSubjectTest, seek_then_write_overwrites)
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

TEST_F(NorSubjectTest, remove_deletes_the_file)
{
    FormatAndMount();
    WriteFile("/file", Pattern(10000, 4));

    EXPECT_EQ(fs::Result::success, runner.Remove("/file"));

    EXPECT_FALSE(runner.Open("/file", metrics::OpenMode::read).has_value());
}

TEST_F(NorSubjectTest, list_returns_the_files_and_directories)
{
    FormatAndMount();
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory"));
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory/inner"));
    WriteFile("/directory/file", Pattern(7, 5));

    auto entries = runner.List("/directory");

    ASSERT_TRUE(entries.has_value());
    EXPECT_THAT(*entries, testing::UnorderedElementsAre(metrics::DirectoryEntry{ "inner", true, 0 }, metrics::DirectoryEntry{ "file", false, 7 }));
}

TEST_F(NorSubjectTest, list_of_a_missing_directory_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.List("/missing").has_value());
    EXPECT_EQ(fs::Result::notFound, runner.Records().back().result);
}

TEST_F(NorSubjectTest, a_file_written_after_expect_size_reads_back)
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

TEST_F(NorSubjectTest, opening_more_files_than_the_handles_reports_too_many_open_files)
{
    FormatAndMount();
    ASSERT_TRUE(runner.Open("/file0", metrics::OpenMode::write).has_value());
    ASSERT_TRUE(runner.Open("/file1", metrics::OpenMode::write).has_value());

    EXPECT_FALSE(runner.Open("/one-more", metrics::OpenMode::write).has_value());
    EXPECT_EQ(fs::Result::tooManyOpenFiles, runner.Records().back().result);
}

TEST_F(NorSubjectTest, usage_grows_with_the_files_written)
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

TEST_F(NorSubjectTest, idle_completes_and_lasts_the_idle_period)
{
    FormatAndMount();
    auto start = time.Now();

    runner.Idle(100ms);

    EXPECT_EQ(start + 100ms, time.Now());
}
