#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/baselines/littlefs/LittleFsSubject.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include "metrics/workload/Pattern.hpp"
#include "metrics/workload/Runner.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::NorModel testNor{ "test-nor", 64, 4096, 256, 1, { 400us, 45ms, 50'000'000 }, { 3ms, 400ms, 50'000'000 } };

    std::vector<uint8_t> Pattern(std::size_t size, uint32_t fileId)
    {
        std::vector<uint8_t> data(size);
        metrics::FillPattern(infra::MakeRange(data), fileId, 0);
        return data;
    }

    class LittleFsSubjectTest
        : public testing::TestWithParam<const metrics::LittleFsConfiguration*>
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
        hal::FlashStub flash{ 64, 4096 };
        metrics::MeteredFlash metered{ flash, testNor };
        metrics::MeteredFlashProbe probe{ metered };
        metrics::LittleFsSubject subject{ time, metered, *GetParam() };
        metrics::Runner runner{ time, subject, probe };
    };
}

INSTANTIATE_TEST_SUITE_P(Default, LittleFsSubjectTest, testing::Values(&metrics::littleFsDefault));
INSTANTIATE_TEST_SUITE_P(RamMatched, LittleFsSubjectTest, testing::Values(&metrics::littleFsRamMatched));

TEST(LittleFsResultTest, maps_littlefs_errors)
{
    EXPECT_EQ(fs::Result::success, metrics::LittleFsResult(LFS_ERR_OK));
    EXPECT_EQ(fs::Result::success, metrics::LittleFsResult(12));
    EXPECT_EQ(fs::Result::notFound, metrics::LittleFsResult(LFS_ERR_NOENT));
    EXPECT_EQ(fs::Result::alreadyExists, metrics::LittleFsResult(LFS_ERR_EXIST));
    EXPECT_EQ(fs::Result::notADirectory, metrics::LittleFsResult(LFS_ERR_NOTDIR));
    EXPECT_EQ(fs::Result::isADirectory, metrics::LittleFsResult(LFS_ERR_ISDIR));
    EXPECT_EQ(fs::Result::notEmpty, metrics::LittleFsResult(LFS_ERR_NOTEMPTY));
    EXPECT_EQ(fs::Result::noSpace, metrics::LittleFsResult(LFS_ERR_NOSPC));
    EXPECT_EQ(fs::Result::noSpace, metrics::LittleFsResult(LFS_ERR_FBIG));
    EXPECT_EQ(fs::Result::nameTooLong, metrics::LittleFsResult(LFS_ERR_NAMETOOLONG));
    EXPECT_EQ(fs::Result::corrupted, metrics::LittleFsResult(LFS_ERR_CORRUPT));
    EXPECT_EQ(fs::Result::invalidPath, metrics::LittleFsResult(LFS_ERR_INVAL));
    EXPECT_EQ(fs::Result::tooManyOpenFiles, metrics::LittleFsResult(LFS_ERR_NOMEM));
    EXPECT_EQ(fs::Result::deviceError, metrics::LittleFsResult(LFS_ERR_IO));
}

TEST_P(LittleFsSubjectTest, is_named_after_its_configuration)
{
    EXPECT_EQ(GetParam()->name, subject.Name());
}

TEST_P(LittleFsSubjectTest, ram_counts_the_filesystem_its_buffers_and_one_open_file)
{
    auto cache = GetParam()->cacheSize;

    EXPECT_EQ(sizeof(lfs_t) + 3 * cache + GetParam()->lookaheadSize + sizeof(lfs_file_t), subject.RamBytes());
}

TEST(LittleFsConfigurationTest, the_ram_matched_configuration_uses_the_ram_budget)
{
    EXPECT_EQ(7680, 3 * metrics::littleFsRamMatched.cacheSize + metrics::littleFsRamMatched.lookaheadSize);
}

TEST_P(LittleFsSubjectTest, mount_of_an_unformatted_device_reports_corruption)
{
    EXPECT_EQ(fs::Result::corrupted, runner.Mount());
}

TEST_P(LittleFsSubjectTest, format_and_mount_use_the_metered_device)
{
    FormatAndMount();

    EXPECT_LT(0, metered.Counters().sectorsErased);
    EXPECT_LT(0, runner.Records().front().latency.count());
}

TEST_P(LittleFsSubjectTest, a_written_file_reads_back)
{
    FormatAndMount();
    auto content = Pattern(10000, 1);

    WriteFile("/file", content);

    EXPECT_EQ(content, ReadFile("/file"));
}

TEST_P(LittleFsSubjectTest, files_survive_a_remount)
{
    FormatAndMount();
    auto content = Pattern(5000, 2);
    WriteFile("/file", content);

    ASSERT_EQ(fs::Result::success, runner.Unmount());
    ASSERT_EQ(fs::Result::success, runner.Mount());

    EXPECT_EQ(content, ReadFile("/file"));
}

TEST_P(LittleFsSubjectTest, opening_a_missing_file_for_reading_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.Open("/missing", metrics::OpenMode::read).has_value());
    EXPECT_EQ(fs::Result::notFound, runner.Records().back().result);
}

TEST_P(LittleFsSubjectTest, append_continues_at_the_end_and_sync_keeps_the_size)
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

TEST_P(LittleFsSubjectTest, seek_then_write_overwrites_in_place)
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

TEST_P(LittleFsSubjectTest, remove_deletes_the_file)
{
    FormatAndMount();
    WriteFile("/file", Pattern(10, 4));

    EXPECT_EQ(fs::Result::success, runner.Remove("/file"));

    EXPECT_FALSE(runner.Open("/file", metrics::OpenMode::read).has_value());
}

TEST_P(LittleFsSubjectTest, list_returns_the_files_and_directories_without_self_and_parent)
{
    FormatAndMount();
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory"));
    ASSERT_EQ(fs::Result::success, runner.MakeDirectory("/directory/inner"));
    WriteFile("/directory/file", Pattern(7, 5));

    auto entries = runner.List("/directory");

    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ((std::vector<metrics::DirectoryEntry>{ { "file", false, 7 }, { "inner", true, 0 } }), *entries);
}

TEST_P(LittleFsSubjectTest, list_of_a_missing_directory_reports_not_found)
{
    FormatAndMount();

    EXPECT_FALSE(runner.List("/missing").has_value());
}

TEST_P(LittleFsSubjectTest, making_a_directory_twice_reports_that_it_exists)
{
    FormatAndMount();
    runner.MakeDirectory("/directory");

    EXPECT_EQ(fs::Result::alreadyExists, runner.MakeDirectory("/directory"));
}

TEST_P(LittleFsSubjectTest, expect_size_is_unsupported)
{
    FormatAndMount();
    auto file = runner.Open("/file", metrics::OpenMode::write);
    ASSERT_TRUE(file.has_value());

    EXPECT_EQ(fs::Result::unsupported, runner.ExpectSize(*file, 4096));
    runner.Close(*file);
}

TEST_P(LittleFsSubjectTest, opening_more_files_than_the_handles_reports_too_many_open_files)
{
    FormatAndMount();
    for (std::size_t index = 0; index != metrics::LittleFsSubject::maxOpenFiles; ++index)
        ASSERT_TRUE(runner.Open("/file" + std::to_string(index), metrics::OpenMode::write).has_value());

    EXPECT_FALSE(runner.Open("/one-more", metrics::OpenMode::write).has_value());
    EXPECT_EQ(fs::Result::tooManyOpenFiles, runner.Records().back().result);
}

TEST_P(LittleFsSubjectTest, a_closed_handle_is_reused)
{
    FormatAndMount();
    auto first = runner.Open("/first", metrics::OpenMode::write);
    ASSERT_TRUE(first.has_value());
    runner.Close(*first);

    EXPECT_EQ(first, runner.Open("/second", metrics::OpenMode::write));
}

TEST_P(LittleFsSubjectTest, writing_beyond_the_capacity_reports_no_space)
{
    FormatAndMount();
    auto file = runner.Open("/large", metrics::OpenMode::write);
    ASSERT_TRUE(file.has_value());
    std::vector<uint8_t> chunk(4096, 0x12);

    auto result = fs::Result::success;
    for (int written = 0; written != 80 && result == fs::Result::success; ++written)
        result = runner.Write(*file, infra::MakeRange(chunk));

    EXPECT_EQ(fs::Result::noSpace, result);
}

TEST_P(LittleFsSubjectTest, usage_grows_with_the_files_written)
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

TEST_P(LittleFsSubjectTest, usage_reports_the_capacity_of_the_device)
{
    FormatAndMount();

    EXPECT_EQ(64 * 4096, runner.QueryUsage()->totalBytes);
}

TEST_P(LittleFsSubjectTest, idle_completes_and_lasts_the_idle_period)
{
    FormatAndMount();
    auto start = time.Now();

    runner.Idle(100ms);

    EXPECT_EQ(start + 100ms, time.Now());
}
