#include "fs/interfaces/test_doubles/FileMock.hpp"
#include "fs/interfaces/test_doubles/FileSystemMock.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/SharedPtr.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "services/hil/HilResponse.hpp"
#include "services/tracer/Tracer.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "validation/firmware/workload/DataLogger.hpp"
#include "validation/firmware/workload/RandomOverwrite.hpp"
#include "validation/firmware/workload/ReadPatterns.hpp"
#include "validation/firmware/workload/test_doubles/WorkloadVolumeMock.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;
using testing::_;

namespace
{
    constexpr validation::FlashGuard guard{ 100ms, 20ms, 2s };

    // The file system completes through the event dispatcher, as fs/ does, so that a workload of many calls does not
    // nest them on the stack
    class WorkloadFailuresTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        WorkloadFailuresTest()
        {
            EXPECT_CALL(volume, FileSystem()).WillRepeatedly(testing::ReturnRef(fileSystem));
            EXPECT_CALL(volume, Counters()).WillRepeatedly(testing::Return(std::nullopt));
            EXPECT_CALL(volume, Format(_)).WillRepeatedly([this](const auto& onDone)
                {
                    Later(onDone, fs::Result::success);
                });
            EXPECT_CALL(fileSystem, Mount(_)).WillRepeatedly([this](const auto& onDone)
                {
                    Later(onDone, fs::Result::success);
                });
            EXPECT_CALL(fileSystem, MakeDirectory(_, _)).WillRepeatedly([this](infra::BoundedConstString, const auto& onDone)
                {
                    Later(onDone, fs::Result::success);
                });
            EXPECT_CALL(fileSystem, Remove(_, _)).WillRepeatedly([this](infra::BoundedConstString, const auto& onDone)
                {
                    Later(onDone, fs::Result::success);
                });
            EXPECT_CALL(fileSystem, Usage(_)).WillRepeatedly([this](const auto& onDone)
                {
                    UsageLater(onDone, fs::Result::success);
                });
            EXPECT_CALL(file, Size()).WillRepeatedly(testing::Return(0));
            EXPECT_CALL(file, Sync(_)).WillRepeatedly([this](const auto& onDone)
                {
                    Later(onDone, fs::Result::success);
                });
            EXPECT_CALL(file, Close(_)).WillRepeatedly([this](const auto& onDone)
                {
                    Later(onDone, fs::Result::success);
                });
        }

        void Later(const infra::Function<void(fs::Result)>& onDone, fs::Result result)
        {
            done = onDone;
            doneResult = result;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    auto callback = done;
                    callback(doneResult);
                });
        }

        void UsageLater(const infra::Function<void(fs::Result, fs::VolumeUsage)>& onDone, fs::Result result)
        {
            usageDone = onDone;
            doneResult = result;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    auto callback = usageDone;
                    callback(doneResult, fs::VolumeUsage{ 0, 1024 * 1024 });
                });
        }

        void OpenLater(const infra::Function<void(fs::Result, infra::SharedPtr<fs::File>)>& onDone, fs::Result result)
        {
            opened = onDone;
            doneResult = result;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    auto callback = opened;
                    callback(doneResult, doneResult == fs::Result::success ? infra::UnOwnedSharedPtr(file) : nullptr);
                });
        }

        void OpenFails(fs::OpenMode mode)
        {
            EXPECT_CALL(fileSystem, Open(_, mode, _)).WillRepeatedly([this](infra::BoundedConstString, fs::OpenMode, const auto& onDone)
                {
                    OpenLater(onDone, fs::Result::notFound);
                });
        }

        void OpenSucceeds(fs::OpenMode mode)
        {
            EXPECT_CALL(fileSystem, Open(_, mode, _)).WillRepeatedly([this](infra::BoundedConstString, fs::OpenMode, const auto& onDone)
                {
                    OpenLater(onDone, fs::Result::success);
                });
        }

        void WritesReturn(fs::Result result)
        {
            EXPECT_CALL(file, Write(_, _)).WillRepeatedly([this, result](infra::ConstByteRange, const auto& onDone)
                {
                    Later(onDone, result);
                });
        }

        void Run(validation::Workload& workload, uint32_t fill = 0)
        {
            validation::WorkloadParameters parameters;
            parameters.scaleDivisor = 256;
            parameters.fillPercent = fill;
            runner.Begin(volume, counting, { "flash", "subject", workload.Name(), fill, 256, 1 }, [] {});
            testing::StrictMock<infra::MockCallback<void()>> finished;
            EXPECT_CALL(finished, callback());
            workload.Run(parameters, [&finished]
                {
                    finished.callback();
                });
            ExecuteAllActions();
            runner.End();
            EXPECT_TRUE(runner.Failed());
        }

        infra::StringOutputStream::WithStorage<64 * 1024> stream;
        services::TracerToStream tracer{ stream };
        services::HilResponse response{ tracer };
        testing::StrictMock<validation::WorkloadVolumeMock> volume;
        testing::StrictMock<fs::FileSystemMock> fileSystem;
        testing::StrictMock<fs::FileMock> file;
        testing::StrictMock<hal::CleanFlashMock> device;
        validation::FakeCycleCounter cycles;
        validation::CountingFlash counting{ device, 256, cycles, validation::FakeCycleCounter::clockHz, guard };
        validation::ResultLines results{ response };
        validation::WorkloadRunner runner{ results, cycles, validation::FakeCycleCounter::clockHz };
        validation::WorkloadTools tools{ runner };
        infra::Function<void(fs::Result)> done;
        infra::Function<void(fs::Result, fs::VolumeUsage)> usageDone;
        infra::Function<void(fs::Result, infra::SharedPtr<fs::File>)> opened;
        infra::Function<void(fs::Result, infra::SharedPtr<fs::Directory>)> directoryOpened;
        fs::Result doneResult{ fs::Result::success };
    };
}

TEST_F(WorkloadFailuresTest, a_fill_ends_at_a_file_it_cannot_create_and_a_log_it_cannot_open_is_skipped)
{
    OpenFails(fs::OpenMode::write);
    OpenFails(fs::OpenMode::append);
    validation::DataLogger workload{ tools };

    Run(workload, 50);
}

TEST_F(WorkloadFailuresTest, a_log_ends_at_an_append_that_fails)
{
    OpenSucceeds(fs::OpenMode::append);
    WritesReturn(fs::Result::noSpace);
    validation::DataLogger workload{ tools };

    Run(workload);
}

TEST_F(WorkloadFailuresTest, a_fill_ends_when_the_usage_fails)
{
    EXPECT_CALL(fileSystem, Usage(_)).WillRepeatedly([this](const auto& onDone)
        {
            UsageLater(onDone, fs::Result::corrupted);
        });
    OpenFails(fs::OpenMode::append);
    validation::DataLogger workload{ tools };

    Run(workload, 50);
}

TEST_F(WorkloadFailuresTest, overwrites_end_at_a_file_that_cannot_be_written)
{
    OpenSucceeds(fs::OpenMode::write);
    WritesReturn(fs::Result::noSpace);
    validation::RandomOverwrite workload{ tools };

    Run(workload);
}

TEST_F(WorkloadFailuresTest, overwrites_end_at_a_file_that_cannot_be_opened_again)
{
    OpenSucceeds(fs::OpenMode::write);
    WritesReturn(fs::Result::success);
    OpenFails(fs::OpenMode::readWrite);
    validation::RandomOverwrite workload{ tools };

    Run(workload);
}

TEST_F(WorkloadFailuresTest, reads_go_on_past_files_and_directories_that_cannot_be_opened)
{
    OpenFails(fs::OpenMode::write);
    OpenFails(fs::OpenMode::read);
    EXPECT_CALL(fileSystem, OpenDirectory(_, _)).WillRepeatedly([this](infra::BoundedConstString, const auto& onDone)
        {
            directoryOpened = onDone;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    auto callback = directoryOpened;
                    callback(fs::Result::notFound, nullptr);
                });
        });
    validation::ReadPatterns workload{ tools };

    Run(workload);
}
