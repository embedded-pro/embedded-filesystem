#include "infra/event/EventDispatcher.hpp"
#include "infra/timer/Timer.hpp"
#include "metrics/workload/test/FakeSubjectFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    class RunnerTest
        : public testing::Test
        , public metrics::FakeSubjectFixture
    {
    public:
        metrics::FileHandle OpenForWriting()
        {
            auto file = runner.Open("/file", metrics::OpenMode::write);
            EXPECT_TRUE(file.has_value());
            return file.value_or(0);
        }

        std::array<uint8_t, 256> data{};
    };
}

TEST_F(RunnerTest, a_call_is_recorded_with_the_current_phase_and_its_result)
{
    runner.SetPhase("L1");

    EXPECT_EQ(fs::Result::success, runner.Format());

    ASSERT_EQ(1, runner.Records().size());
    EXPECT_EQ(metrics::Operation::format, runner.Records()[0].operation);
    EXPECT_EQ("L1", runner.Records()[0].phase);
    EXPECT_EQ(fs::Result::success, runner.Records()[0].result);
}

TEST(OperationNameTest, names_every_operation)
{
    EXPECT_EQ("format", metrics::OperationName(metrics::Operation::format));
    EXPECT_EQ("seek", metrics::OperationName(metrics::Operation::seek));
    EXPECT_EQ("expectSize", metrics::OperationName(metrics::Operation::expectSize));
    EXPECT_EQ("idle", metrics::OperationName(metrics::Operation::idle));
}

TEST_F(RunnerTest, the_phase_observer_hears_of_every_phase_that_ends)
{
    std::vector<std::string> ended;
    runner.ObservePhases([&ended](const std::string& phase)
        {
            ended.push_back(phase);
        });

    runner.SetPhase("a");
    runner.SetPhase("a");
    runner.SetPhase("b");

    EXPECT_EQ((std::vector<std::string>{ "", "a" }), ended);
}

TEST_F(RunnerTest, a_write_records_its_user_bytes_latency_and_device_work)
{
    auto file = OpenForWriting();

    EXPECT_EQ(fs::Result::success, runner.Write(file, infra::MakeRange(data)));

    const auto& record = runner.Records().back();
    EXPECT_EQ(metrics::Operation::write, record.operation);
    EXPECT_EQ(256, record.userBytes);
    EXPECT_EQ(0, record.fileSizeBefore);
    EXPECT_EQ(400us, record.latency);
    EXPECT_EQ((metrics::DeviceSnapshot{ .writeRequests = 1, .bytesWritten = 256, .deviceTime = 400us }), record.device);
}

TEST_F(RunnerTest, a_write_records_the_file_size_before_the_call)
{
    auto file = OpenForWriting();
    runner.Write(file, infra::MakeRange(data));

    runner.Write(file, infra::MakeRange(data));

    EXPECT_EQ(256, runner.Records().back().fileSizeBefore);
}

TEST_F(RunnerTest, a_read_records_the_bytes_actually_read)
{
    subject.files["/file"] = std::vector<uint8_t>(10, 0x5a);
    auto file = runner.Open("/file", metrics::OpenMode::read);
    ASSERT_TRUE(file.has_value());

    EXPECT_EQ(10, runner.Read(*file, infra::MakeRange(data)));

    const auto& record = runner.Records().back();
    EXPECT_EQ(metrics::Operation::read, record.operation);
    EXPECT_EQ(10, record.userBytes);
    EXPECT_EQ(10, record.fileSizeBefore);
    EXPECT_EQ(10, record.device.bytesRead);
    EXPECT_EQ(0x5a, data[9]);
}

TEST_F(RunnerTest, a_failed_open_returns_no_handle_and_records_the_result)
{
    EXPECT_FALSE(runner.Open("/missing", metrics::OpenMode::read).has_value());

    EXPECT_EQ(fs::Result::notFound, runner.Records().back().result);
}

TEST_F(RunnerTest, a_seek_is_recorded_with_the_file_size)
{
    auto file = OpenForWriting();
    runner.Write(file, infra::MakeRange(data));

    runner.Seek(file, 10);

    const auto& record = runner.Records().back();
    EXPECT_EQ(metrics::Operation::seek, record.operation);
    EXPECT_EQ(256, record.fileSizeBefore);
    EXPECT_EQ(metrics::DeviceSnapshot{}, record.device);
}

TEST_F(RunnerTest, list_returns_the_entries_of_the_directory)
{
    subject.files["/directory/a"] = std::vector<uint8_t>(3);
    subject.files["/directory/b"] = std::vector<uint8_t>(5);

    auto entries = runner.List("/directory");

    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ((std::vector<metrics::DirectoryEntry>{ { "a", false, 3 }, { "b", false, 5 } }), *entries);
}

TEST_F(RunnerTest, a_call_completed_from_inside_the_call_aborts)
{
    subject.completeSynchronously = true;

    EXPECT_DEATH(runner.Format(), "");
}

TEST_F(RunnerTest, a_call_that_never_completes_interrupts_the_runner_without_a_record)
{
    subject.neverComplete = true;

    EXPECT_EQ(fs::Result::deviceError, runner.Format());

    EXPECT_TRUE(runner.Interrupted());
    EXPECT_TRUE(runner.Records().empty());
}

TEST_F(RunnerTest, a_call_still_open_after_ten_minutes_interrupts_the_runner)
{
    infra::TimerRepeating periodic{ 1s, [] {} };
    subject.neverComplete = true;
    auto start = time.Now();

    EXPECT_EQ(fs::Result::deviceError, runner.Format());

    EXPECT_TRUE(runner.Interrupted());
    EXPECT_EQ(start + 10min, time.Now());
}

TEST_F(RunnerTest, calls_after_an_interruption_do_not_reach_the_subject)
{
    subject.neverComplete = true;
    runner.Format();
    subject.neverComplete = false;

    EXPECT_EQ(fs::Result::deviceError, runner.MakeDirectory("/directory"));

    EXPECT_TRUE(subject.directories.empty());
    EXPECT_TRUE(runner.Records().empty());
}

TEST_F(RunnerTest, an_interrupted_read_leaves_earlier_records_unchanged)
{
    subject.files["/file"] = std::vector<uint8_t>(10, 0x5a);
    auto file = runner.Open("/file", metrics::OpenMode::read);
    ASSERT_TRUE(file.has_value());
    runner.Read(*file, infra::MakeRange(data));
    subject.neverComplete = true;

    EXPECT_EQ(0, runner.Read(*file, infra::MakeRange(data)));

    ASSERT_EQ(2, runner.Records().size());
    EXPECT_EQ(10, runner.Records()[1].userBytes);
}

TEST_F(RunnerTest, idle_lasts_the_full_duration_and_asks_the_subject_for_background_work)
{
    auto start = time.Now();

    runner.Idle(100ms);

    EXPECT_EQ(start + 100ms, time.Now());
    EXPECT_EQ(1, subject.idleHints);
    EXPECT_EQ(metrics::Operation::idle, runner.Records().back().operation);
    EXPECT_EQ(100ms, runner.Records().back().latency);
}

TEST_F(RunnerTest, idle_records_device_work_done_in_the_background)
{
    infra::TimerSingleShot background{ 50ms, [this]
        {
            metered.WriteBuffer(infra::MakeRange(data), 0, [] {});
        } };

    runner.Idle(100ms);

    EXPECT_EQ(1, runner.Records().back().device.writeRequests);
}

TEST_F(RunnerTest, settle_runs_until_the_device_is_idle_and_records_the_work)
{
    auto start = time.Now();
    infra::EventDispatcher::Instance().Schedule([this]
        {
            metered.WriteBuffer(infra::MakeRange(data), 0, [] {});
        });

    EXPECT_TRUE(runner.Settle(1s));

    EXPECT_EQ(start + 400us, time.Now());
    EXPECT_EQ(1, runner.Records().back().device.writeRequests);
}

TEST_F(RunnerTest, settle_returns_false_when_work_continues_past_the_limit)
{
    infra::TimerRepeating periodic{ 1ms, [] {} };

    EXPECT_FALSE(runner.Settle(10ms));
}
