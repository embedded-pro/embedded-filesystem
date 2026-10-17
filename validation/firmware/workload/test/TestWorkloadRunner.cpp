#include "fs/interfaces/test_doubles/DirectoryMock.hpp"
#include "fs/interfaces/test_doubles/FileMock.hpp"
#include "fs/interfaces/test_doubles/FileSystemMock.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/util/SharedPtr.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "validation/firmware/workload/WorkloadRunner.hpp"
#include "validation/firmware/workload/test_doubles/WorkloadVolumeMock.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

using namespace std::chrono_literals;
using testing::_;

namespace
{
    constexpr validation::FlashGuard guard{ 100ms, 20ms, 2s };

    class WorkloadRunnerTest
        : public services::HilFixture
    {
    public:
        WorkloadRunnerTest()
        {
            EXPECT_CALL(volume, FileSystem()).WillRepeatedly(testing::ReturnRef(fileSystem));
            EXPECT_CALL(volume, Counters()).WillRepeatedly(testing::Invoke([this]
                {
                    return counters;
                }));
        }

        void Begin(validation::PhaseWindow window = validation::PhaseWindow::none)
        {
            runner.Begin(volume, flash, { "qspi", "embedded-filesystem/nor", "L2", 50, 16, 1000 }, [this]
                {
                    interrupted.callback();
                });
            runner.SetPhase("P", window);
            Output();
        }

        infra::Function<void(fs::Result result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void Complete(fs::Result result = fs::Result::success)
        {
            EXPECT_CALL(done, callback(result));
            pending(result);
        }

        void OpenFile()
        {
            EXPECT_CALL(fileSystem, Open(_, fs::OpenMode::write, _)).WillOnce([this](infra::BoundedConstString, fs::OpenMode, const auto& onDone)
                {
                    onDone(fs::Result::success, infra::UnOwnedSharedPtr(file));
                });
            EXPECT_CALL(file, Size()).WillRepeatedly(testing::Return(0));
            EXPECT_CALL(done, callback(fs::Result::success));
            runner.Open("/f", fs::OpenMode::write, Done());
        }

        void ReadFromDevice()
        {
            EXPECT_CALL(device, ReadBuffer(_, 0, _)).WillOnce(testing::SaveArg<2>(&deviceDone));
            flash.ReadBuffer(infra::MakeRange(buffer), 0, [] {});
        }

        void Write(uint64_t checkpoints = 0)
        {
            EXPECT_CALL(file, Write(_, _)).WillOnce(testing::SaveArg<1>(&pending));
            runner.Write(infra::MakeRange(buffer), Done());
            counters->checkpoints += checkpoints;
            Complete();
        }

        std::string End()
        {
            runner.End();
            return Output();
        }

        testing::StrictMock<validation::WorkloadVolumeMock> volume;
        testing::StrictMock<fs::FileSystemMock> fileSystem;
        testing::StrictMock<fs::FileMock> file;
        testing::StrictMock<fs::DirectoryMock> directory;
        testing::StrictMock<hal::CleanFlashMock> device;
        validation::FakeCycleCounter cycles;
        validation::CountingFlash flash{ device, 256, cycles, validation::FakeCycleCounter::clockHz, guard };
        validation::ResultLines results{ response };
        validation::WorkloadRunner runner{ results, cycles, validation::FakeCycleCounter::clockHz };
        std::optional<validation::VolumeCounts> counters{ validation::VolumeCounts{} };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
        testing::StrictMock<infra::MockCallback<void()>> interrupted;
        infra::Function<void(fs::Result)> pending;
        infra::Function<void()> deviceDone;
        std::array<uint8_t, 64> buffer{};
    };
}

TEST_F(WorkloadRunnerTest, begin_announces_the_workload)
{
    runner.Begin(volume, flash, { "qspi", "embedded-filesystem/nor", "L1", 0, 16, 1000 }, [] {});

    EXPECT_EQ("\r\nEVT workload id=1 device=qspi subject=embedded-filesystem/nor workload=L1 fill=0 scale=16 ramBytes=1000\r\n", Output());
    EXPECT_TRUE(runner.Running());
}

TEST_F(WorkloadRunnerTest, a_call_is_added_to_its_phase_with_its_latency_and_the_requests_to_the_device)
{
    Begin();
    EXPECT_CALL(volume, Format(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Format(Done());
    ReadFromDevice();
    ForwardTime(5ms);
    deviceDone();

    Complete();

    auto output = End();
    EXPECT_THAT(output, testing::HasSubstr("phase=P fill=50 ramBytes=1000 elapsedNs=5000000 readRequests=1 bytesRead=64 readNs=5000000 "));
    EXPECT_THAT(output, testing::HasSubstr("format.calls=1 format.userBytes=0 format.latencyNs=5000000 format.maxNs=5000000 format.histogram=84,1"));
    EXPECT_FALSE(runner.Running());
}

TEST_F(WorkloadRunnerTest, a_new_phase_prints_the_one_that_ended)
{
    Begin();

    runner.SetPhase("Q");

    EXPECT_THAT(Output(), testing::HasSubstr("EVT phase id=2 device=qspi subject=embedded-filesystem/nor workload=L2 phase=P "));
    EXPECT_THAT(End(), testing::HasSubstr("phase=Q "));
}

TEST_F(WorkloadRunnerTest, the_volume_counts_continue_across_mounts)
{
    Begin();
    counters->allocatedSectors = 5;
    EXPECT_CALL(fileSystem, Mount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Mount(Done());
    counters->allocatedSectors = 1;

    Complete();

    EXPECT_THAT(End(), testing::HasSubstr("allocatedSectors=6 "));
}

TEST_F(WorkloadRunnerTest, a_failed_mount_keeps_the_volume_counts)
{
    Begin();
    counters->allocatedSectors = 5;
    EXPECT_CALL(fileSystem, Mount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Mount(Done());

    Complete(fs::Result::corrupted);

    EXPECT_THAT(End(), testing::HasSubstr("allocatedSectors=5 "));
}

TEST_F(WorkloadRunnerTest, a_volume_without_counts_prints_none)
{
    counters.reset();
    Begin(validation::PhaseWindow::firstLog);
    EXPECT_CALL(fileSystem, Mount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Mount(Done());
    Complete();
    OpenFile();
    Write();

    auto output = End();
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("allocatedSectors")));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("EVT window")));
}

TEST_F(WorkloadRunnerTest, a_call_that_does_not_succeed_is_reported_once_at_the_end)
{
    Begin();
    EXPECT_CALL(fileSystem, Remove(_, _)).Times(2).WillRepeatedly(testing::SaveArg<1>(&pending));
    runner.Remove("/a", Done());
    Complete(fs::Result::notFound);
    runner.Remove("/b", Done());
    Complete(fs::Result::busy);

    EXPECT_TRUE(runner.Failed());
    EXPECT_THAT(End(), testing::HasSubstr("EVT workload.failed id=3 device=qspi subject=embedded-filesystem/nor workload=L2 fill=50 phase=P op=remove result=notFound\r\n"));
}

TEST_F(WorkloadRunnerTest, an_unsupported_expected_size_is_no_failure)
{
    Begin();
    OpenFile();
    EXPECT_CALL(file, ExpectSize(4096, _)).WillOnce(testing::SaveArg<1>(&pending));
    runner.ExpectSize(4096, Done());

    Complete(fs::Result::unsupported);

    EXPECT_FALSE(runner.Failed());
    EXPECT_THAT(End(), testing::HasSubstr("expectSize.calls=1"));
}

TEST_F(WorkloadRunnerTest, file_calls_go_to_the_open_file)
{
    Begin();
    OpenFile();
    Write();
    EXPECT_CALL(file, Seek(64));
    runner.Seek(64);
    EXPECT_CALL(file, Sync(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Sync(Done());
    Complete();
    EXPECT_CALL(file, Close(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Close(Done());
    Complete();

    EXPECT_EQ(0, runner.FileSize());
    auto output = End();
    EXPECT_THAT(output, testing::HasSubstr("open.calls=1 "));
    EXPECT_THAT(output, testing::HasSubstr("write.calls=1 write.userBytes=64 "));
    EXPECT_THAT(output, testing::HasSubstr("seek.calls=1 "));
    EXPECT_THAT(output, testing::HasSubstr("sync.calls=1 "));
    EXPECT_THAT(output, testing::HasSubstr("close.calls=1 "));
}

TEST_F(WorkloadRunnerTest, a_read_counts_the_bytes_it_read)
{
    Begin();
    OpenFile();
    infra::Function<void(fs::Result, std::size_t)> onRead;
    EXPECT_CALL(file, Read(_, _)).WillOnce(testing::SaveArg<1>(&onRead));
    testing::StrictMock<infra::MockCallback<void(fs::Result, std::size_t)>> read;
    runner.Read(infra::MakeRange(buffer), [&read](fs::Result result, std::size_t bytesRead)
        {
            read.callback(result, bytesRead);
        });

    EXPECT_CALL(read, callback(fs::Result::success, 10));
    onRead(fs::Result::success, 10);

    EXPECT_THAT(End(), testing::HasSubstr("read.calls=1 read.userBytes=10 "));
}

TEST_F(WorkloadRunnerTest, volume_calls_go_to_the_file_system)
{
    Begin();
    EXPECT_CALL(fileSystem, MakeDirectory(_, _)).WillOnce(testing::SaveArg<1>(&pending));
    runner.MakeDirectory("/d", Done());
    Complete();
    EXPECT_CALL(fileSystem, Unmount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Unmount(Done());
    Complete();
    EXPECT_CALL(fileSystem, Usage(_)).WillOnce([](const auto& onDone)
        {
            onDone(fs::Result::success, fs::VolumeUsage{ 10, 100 });
        });
    testing::StrictMock<infra::MockCallback<void(fs::Result, uint64_t)>> usage;
    EXPECT_CALL(usage, callback(fs::Result::success, 10));
    runner.Usage([&usage](fs::Result result, fs::VolumeUsage reported)
        {
            usage.callback(result, reported.usedBytes);
        });

    auto output = End();
    EXPECT_THAT(output, testing::HasSubstr("unmount.calls=1 "));
    EXPECT_THAT(output, testing::HasSubstr("makeDirectory.calls=1 "));
    EXPECT_THAT(output, testing::HasSubstr("usage.calls=1 "));
}

TEST_F(WorkloadRunnerTest, a_list_reads_every_entry_of_the_directory)
{
    Begin();
    EXPECT_CALL(fileSystem, OpenDirectory(_, _)).WillOnce([this](infra::BoundedConstString, const auto& onDone)
        {
            onDone(fs::Result::success, infra::UnOwnedSharedPtr(directory));
        });
    int entries = 0;
    EXPECT_CALL(directory, Next(_)).Times(3).WillRepeatedly([&entries](const auto& onDone)
        {
            if (entries++ < 2)
                onDone(fs::Result::success, fs::EntryInfo{});
            else
                onDone(fs::Result::success, std::nullopt);
        });
    EXPECT_CALL(done, callback(fs::Result::success));

    runner.List("/d", Done());

    EXPECT_THAT(End(), testing::HasSubstr("list.calls=1 "));
}

TEST_F(WorkloadRunnerTest, a_list_of_a_missing_directory_fails)
{
    Begin();
    EXPECT_CALL(fileSystem, OpenDirectory(_, _)).WillOnce([](infra::BoundedConstString, const auto& onDone)
        {
            onDone(fs::Result::notFound, nullptr);
        });
    EXPECT_CALL(done, callback(fs::Result::notFound));

    runner.List("/d", Done());

    EXPECT_TRUE(runner.Failed());
}

TEST_F(WorkloadRunnerTest, an_idle_period_lasts_its_duration)
{
    Begin();
    testing::StrictMock<infra::MockCallback<void()>> idle;
    runner.Idle(100ms, [&idle]
        {
            idle.callback();
        });

    ForwardTime(99ms);
    EXPECT_CALL(idle, callback());
    ForwardTime(1ms);

    EXPECT_THAT(End(), testing::HasSubstr("idle.calls=1 idle.userBytes=0 idle.latencyNs=100000000 "));
}

TEST_F(WorkloadRunnerTest, settling_ends_once_the_volume_is_idle)
{
    Begin();
    testing::StrictMock<infra::MockCallback<void()>> settled;
    EXPECT_CALL(fileSystem, Idle()).WillOnce(testing::Return(false)).WillOnce(testing::Return(false)).WillOnce(testing::Return(true));
    runner.Settle(10min, [&settled]
        {
            settled.callback();
        });

    ForwardTime(2ms);
    EXPECT_CALL(settled, callback());
    ForwardTime(1ms);

    EXPECT_THAT(End(), testing::HasSubstr("idle.calls=1 idle.userBytes=0 idle.latencyNs=3000000 "));
}

TEST_F(WorkloadRunnerTest, settling_ends_at_its_limit)
{
    Begin();
    testing::StrictMock<infra::MockCallback<void()>> settled;
    EXPECT_CALL(fileSystem, Idle()).WillRepeatedly(testing::Return(false));
    runner.Settle(10ms, [&settled]
        {
            settled.callback();
        });

    EXPECT_CALL(settled, callback());
    ForwardTime(10ms);
}

TEST_F(WorkloadRunnerTest, appends_of_the_first_log_make_its_window)
{
    Begin(validation::PhaseWindow::firstLog);
    OpenFile();

    for (int append = 0; append != 5; ++append)
        Write(1);

    EXPECT_THAT(End(), testing::HasSubstr("EVT window id=3 device=qspi subject=embedded-filesystem/nor workload=L2 fill=50 log=first appends=4 "));
}

TEST_F(WorkloadRunnerTest, appends_of_the_second_log_make_its_window_from_4_kib)
{
    Begin(validation::PhaseWindow::secondLog);
    OpenFile();
    EXPECT_CALL(file, Size()).WillRepeatedly(testing::Return(4096));

    for (int append = 0; append != 5; ++append)
        Write(1);

    EXPECT_THAT(End(), testing::HasSubstr("log=second appends=4 "));
}

TEST_F(WorkloadRunnerTest, calls_outside_the_logs_make_no_window)
{
    Begin();
    OpenFile();

    for (int append = 0; append != 5; ++append)
        Write(1);

    EXPECT_THAT(End(), testing::Not(testing::HasSubstr("EVT window")));
}

TEST_F(WorkloadRunnerTest, a_request_that_outlasts_its_guard_interrupts_the_workload)
{
    Begin();
    EXPECT_CALL(volume, Format(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Format(Done());
    ReadFromDevice();

    EXPECT_CALL(interrupted, callback());
    ForwardTime(100ms);

    EXPECT_TRUE(runner.Interrupted());
    EXPECT_FALSE(runner.Running());
    EXPECT_THAT(Output(), testing::HasSubstr("EVT workload.failed id=2 device=qspi subject=embedded-filesystem/nor workload=L2 fill=50 phase=P op=format result=timeout\r\n"));
    pending(fs::Result::success);
}

TEST_F(WorkloadRunnerTest, a_call_that_outlasts_its_limit_interrupts_the_workload)
{
    Begin();
    EXPECT_CALL(fileSystem, Mount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Mount(Done());

    EXPECT_CALL(interrupted, callback());
    ForwardTime(validation::WorkloadRunner::callLimit);

    EXPECT_TRUE(runner.Interrupted());
}

TEST_F(WorkloadRunnerTest, late_completions_after_an_interruption_are_ignored)
{
    Begin();
    OpenFile();
    infra::Function<void(fs::Result, std::size_t)> onRead;
    EXPECT_CALL(file, Read(_, _)).WillOnce(testing::SaveArg<1>(&onRead));
    runner.Read(infra::MakeRange(buffer), [](fs::Result, std::size_t) {});
    EXPECT_CALL(interrupted, callback());
    ForwardTime(validation::WorkloadRunner::callLimit);
    onRead(fs::Result::success, 1);

    EXPECT_CALL(fileSystem, Usage(_)).WillOnce([](const auto& onDone)
        {
            onDone(fs::Result::success, fs::VolumeUsage{});
        });
    runner.Usage([](fs::Result, fs::VolumeUsage) {});
}

TEST_F(WorkloadRunnerTest, a_list_stops_reading_entries_after_an_interruption)
{
    Begin();
    infra::Function<void(fs::Result, std::optional<fs::EntryInfo>)> onNext;
    EXPECT_CALL(fileSystem, OpenDirectory(_, _)).WillOnce([this](infra::BoundedConstString, const auto& onDone)
        {
            onDone(fs::Result::success, infra::UnOwnedSharedPtr(directory));
        });
    EXPECT_CALL(directory, Next(_)).WillOnce(testing::SaveArg<0>(&onNext));
    runner.List("/d", Done());
    EXPECT_CALL(interrupted, callback());
    ForwardTime(validation::WorkloadRunner::callLimit);

    onNext(fs::Result::success, fs::EntryInfo{});
}

TEST_F(WorkloadRunnerTest, release_completes_at_once_when_no_volume_is_mounted)
{
    testing::StrictMock<infra::MockCallback<void()>> released;
    EXPECT_CALL(released, callback());

    runner.Release([&released]
        {
            released.callback();
        });
}

TEST_F(WorkloadRunnerTest, release_unmounts_the_volume_an_earlier_workload_mounted)
{
    Begin();
    EXPECT_CALL(fileSystem, Mount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Mount(Done());
    Complete();
    runner.End();

    testing::StrictMock<infra::MockCallback<void()>> released;
    infra::Function<void(fs::Result)> onUnmounted;
    EXPECT_CALL(fileSystem, Unmount(_)).WillOnce(testing::SaveArg<0>(&onUnmounted));
    runner.Release([&released]
        {
            released.callback();
        });
    EXPECT_CALL(released, callback());
    onUnmounted(fs::Result::success);

    EXPECT_CALL(released, callback());
    runner.Release([&released]
        {
            released.callback();
        });
}

TEST_F(WorkloadRunnerTest, an_unmount_in_the_workload_leaves_nothing_to_release)
{
    Begin();
    EXPECT_CALL(fileSystem, Mount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Mount(Done());
    Complete();
    EXPECT_CALL(fileSystem, Unmount(_)).WillOnce(testing::SaveArg<0>(&pending));
    runner.Unmount(Done());
    Complete();
    runner.End();

    testing::StrictMock<infra::MockCallback<void()>> released;
    EXPECT_CALL(released, callback());
    runner.Release([&released]
        {
            released.callback();
        });
}
