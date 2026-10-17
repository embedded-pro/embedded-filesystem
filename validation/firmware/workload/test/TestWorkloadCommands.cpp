#include "fs/interfaces/test_doubles/FileSystemMock.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "validation/firmware/workload/WorkloadCommands.hpp"
#include "validation/firmware/workload/test_doubles/WorkloadMock.hpp"
#include "validation/firmware/workload/test_doubles/WorkloadVolumeMock.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

using namespace std::chrono_literals;
using testing::_;

namespace
{
    class WorkloadCommandsTest
        : public services::HilFixture
    {
    public:
        WorkloadCommandsTest()
        {
            EXPECT_CALL(logger, Name()).WillRepeatedly(testing::Return("L2"));
            EXPECT_CALL(logger, Fills()).WillRepeatedly(testing::Return(true));
            EXPECT_CALL(writer, Name()).WillRepeatedly(testing::Return("L1"));
            EXPECT_CALL(writer, Fills()).WillRepeatedly(testing::Return(false));
            EXPECT_CALL(volume, Subject()).WillRepeatedly(testing::Return("embedded-filesystem/nor"));
            EXPECT_CALL(volume, RamBytes()).WillRepeatedly(testing::Return(1000));
            EXPECT_CALL(volume, Counters()).WillRepeatedly(testing::Return(std::nullopt));
            EXPECT_CALL(volume, FileSystem()).WillRepeatedly(testing::ReturnRef(fileSystem));
        }

        void ExpectRun(validation::WorkloadMock& workload)
        {
            EXPECT_CALL(workload, Run(_, _)).WillOnce([this](const validation::WorkloadParameters& parameters, const infra::Function<void()>& onDone)
                {
                    ran = parameters;
                    finish = onDone;
                });
        }

        testing::StrictMock<validation::WorkloadMock> logger;
        testing::StrictMock<validation::WorkloadMock> writer;
        testing::StrictMock<validation::WorkloadVolumeMock> volume;
        testing::StrictMock<fs::FileSystemMock> fileSystem;
        testing::StrictMock<hal::CleanFlashMock> device;
        validation::FakeCycleCounter cycles;
        validation::CountingFlash flash{ device, 256, cycles, validation::FakeCycleCounter::clockHz, { 100ms, 20ms, 2s } };
        std::array<validation::DeviceEntry, 2> devices{ {
            { { "qspi", validation::DeviceKind::nor, 8, 4096, 256, "", 0, "" }, true },
            { { "fmc", validation::DeviceKind::nor, 8, 4096, 64, "", 0, "" }, false },
        } };
        const std::array<validation::WorkloadTarget, 2> targets{ { { devices[0], flash, volume }, { devices[1], flash, volume } } };
        std::array<validation::Workload*, 2> workloads{ { &logger, &writer } };
        validation::ResultLines results{ response };
        validation::WorkloadRunner runner{ results, cycles, validation::FakeCycleCounter::clockHz };
        validation::WorkloadCommands commands{ context, runner, infra::MakeRange(workloads), infra::MakeRange(targets) };
        validation::WorkloadParameters ran;
        infra::Function<void()> finish;
    };
}

TEST_F(WorkloadCommandsTest, run_starts_the_workload_at_the_default_scale_without_fill_and_answers_ok_when_it_ends)
{
    ExpectRun(logger);
    Execute("wl.run qspi L2");

    EXPECT_EQ(16, ran.scaleDivisor);
    EXPECT_EQ(0, ran.fillPercent);
    EXPECT_EQ("EVT workload id=1 device=qspi subject=embedded-filesystem/nor workload=L2 fill=0 scale=16 ramBytes=1000\r\n", Output());

    finish();
    EXPECT_EQ("\r\nOK\r\n", Output());
}

TEST_F(WorkloadCommandsTest, run_takes_a_fill_level_and_a_scale)
{
    ExpectRun(logger);
    Execute("wl.run qspi L2 fill=50 scale=4");

    EXPECT_EQ(4, ran.scaleDivisor);
    EXPECT_EQ(50, ran.fillPercent);
    finish();
}

TEST_F(WorkloadCommandsTest, run_takes_a_device_a_workload_and_its_keys)
{
    for (const char* command : { "wl.run qspi", "wl.run", "wl.run qspi L2 other", "wl.run qspi L2 n=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR usage\r\n", Output()) << command;
    }
}

TEST_F(WorkloadCommandsTest, fill_levels_and_scales_out_of_range_are_rejected)
{
    for (const char* command : { "wl.run qspi L2 fill=100", "wl.run qspi L2 scale=0", "wl.run qspi L2 scale=1025", "wl.run qspi L1 fill=10" })
    {
        Execute(command);
        EXPECT_EQ("ERR range\r\n", Output()) << command;
    }
}

TEST_F(WorkloadCommandsTest, a_device_that_is_absent_or_unknown_and_an_unknown_workload_are_unsupported)
{
    for (const char* command : { "wl.run fmc L2", "wl.run nand L2", "wl.run qspi L5", "fs.format fmc" })
    {
        Execute(command);
        EXPECT_EQ("ERR unsupported\r\n", Output()) << command;
    }
}

TEST_F(WorkloadCommandsTest, commands_while_a_workload_runs_are_busy)
{
    ExpectRun(logger);
    Execute("wl.run qspi L2");
    Output();

    for (const char* command : { "wl.run qspi L1", "fs.format qspi" })
    {
        Execute(command);
        EXPECT_EQ("ERR busy\r\n", Output()) << command;
    }

    finish();
}

TEST_F(WorkloadCommandsTest, a_workload_with_a_call_that_failed_answers_failed)
{
    EXPECT_CALL(logger, Run(_, _)).WillOnce([this](const validation::WorkloadParameters&, const infra::Function<void()>& onDone)
        {
            EXPECT_CALL(fileSystem, Remove(_, _)).WillOnce([](infra::BoundedConstString, const auto& removed)
                {
                    removed(fs::Result::notFound);
                });
            finish = onDone;
            runner.SetPhase("L2");
            runner.Remove("/a", [this](fs::Result)
                {
                    finish();
                });
        });

    Execute("wl.run qspi L2");

    EXPECT_THAT(Output(), testing::EndsWith("result=notFound\r\nERR failed\r\n"));
}

TEST_F(WorkloadCommandsTest, a_workload_unmounts_the_volume_it_leaves_mounted_before_it_answers)
{
    infra::Function<void(fs::Result)> unmounted;
    EXPECT_CALL(fileSystem, Mount(_)).WillOnce([](const auto& mounted)
        {
            mounted(fs::Result::success);
        });
    EXPECT_CALL(logger, Run(_, _)).WillOnce([this](const validation::WorkloadParameters&, const infra::Function<void()>& onDone)
        {
            finish = onDone;
            runner.Mount([this](fs::Result)
                {
                    finish();
                });
        });
    EXPECT_CALL(fileSystem, Unmount(_)).WillOnce(testing::SaveArg<0>(&unmounted));
    Execute("wl.run qspi L2");
    EXPECT_THAT(Output(), testing::Not(testing::HasSubstr("OK")));

    unmounted(fs::Result::success);

    EXPECT_THAT(Output(), testing::EndsWith("\r\nOK\r\n"));
}

TEST_F(WorkloadCommandsTest, a_workload_that_a_request_outlasts_answers_timeout)
{
    infra::Function<void()> read;
    std::array<uint8_t, 4> buffer{};
    EXPECT_CALL(device, ReadBuffer(_, 0, _)).WillOnce(testing::SaveArg<2>(&read));
    EXPECT_CALL(logger, Run(_, _)).WillOnce([this, &buffer](const validation::WorkloadParameters&, const infra::Function<void()>&)
        {
            flash.ReadBuffer(infra::MakeRange(buffer), 0, [] {});
        });
    Execute("wl.run qspi L2");

    ForwardTime(100ms);

    EXPECT_THAT(Output(), testing::EndsWith("result=timeout\r\n\r\nERR timeout\r\n"));
    read();
}

TEST_F(WorkloadCommandsTest, format_formats_the_volume_of_the_device)
{
    infra::Function<void(fs::Result)> formatted;
    EXPECT_CALL(volume, Format(_)).WillOnce(testing::SaveArg<0>(&formatted));
    Execute("fs.format qspi");
    EXPECT_EQ("", Output());

    formatted(fs::Result::success);

    EXPECT_EQ("\r\nOK\r\n", Output());
}

TEST_F(WorkloadCommandsTest, a_format_that_fails_answers_failed)
{
    EXPECT_CALL(volume, Format(_)).WillOnce([](const auto& formatted)
        {
            formatted(fs::Result::deviceError);
        });

    Execute("fs.format qspi");

    EXPECT_EQ("ERR failed\r\n", Output());
}

TEST_F(WorkloadCommandsTest, format_takes_one_device)
{
    for (const char* command : { "fs.format", "fs.format qspi fmc", "fs.format qspi n=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR usage\r\n", Output()) << command;
    }
}

TEST_F(WorkloadCommandsTest, a_completion_after_the_command_timed_out_answers_nothing)
{
    infra::Function<void(fs::Result)> formatted;
    EXPECT_CALL(volume, Format(_)).WillOnce(testing::SaveArg<0>(&formatted));
    Execute("fs.format qspi");
    ForwardTime(validation::WorkloadCommands::formatTimeout);
    Output();

    formatted(fs::Result::success);

    EXPECT_EQ("", Output());
}

TEST_F(WorkloadCommandsTest, an_end_after_the_command_timed_out_answers_nothing)
{
    ExpectRun(logger);
    Execute("wl.run qspi L2");
    ForwardTime(validation::WorkloadCommands::runTimeout);
    Output();

    finish();

    EXPECT_THAT(Output(), testing::Not(testing::HasSubstr("OK")));
}
