#include "metrics/workload/Runner.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>
#include <chrono>

namespace metrics
{
    namespace
    {
        constexpr std::chrono::minutes callLimit{ 10 };
    }

    std::string_view OperationName(Operation operation)
    {
        static constexpr std::array<std::string_view, 15> names{ "format", "mount", "unmount", "open", "read", "write", "seek", "sync", "close", "expectSize", "remove", "makeDirectory", "list", "usage", "idle" };
        return names[static_cast<std::size_t>(operation)];
    }

    Runner::Runner(VirtualTime& time, FileSystemUnderTest& subject, const DeviceProbe& device)
        : time(time)
        , subject(subject)
        , device(device)
    {}

    void Runner::SetPhase(std::string newPhase)
    {
        if (phaseObserver && newPhase != phase)
            phaseObserver(phase);

        phase = std::move(newPhase);
    }

    void Runner::ObservePhases(PhaseObserver observer)
    {
        phaseObserver = std::move(observer);
    }

    fs::Result Runner::Format()
    {
        return Call(Operation::format, 0, 0, [this](const Completion& done)
            {
                subject.Format(done);
            });
    }

    fs::Result Runner::Mount()
    {
        return Call(Operation::mount, 0, 0, [this](const Completion& done)
            {
                subject.Mount(done);
            });
    }

    fs::Result Runner::Unmount()
    {
        return Call(Operation::unmount, 0, 0, [this](const Completion& done)
            {
                subject.Unmount(done);
            });
    }

    std::optional<FileHandle> Runner::Open(std::string_view path, OpenMode mode)
    {
        FileHandle handle{};
        auto result = Call(Operation::open, 0, 0, [this, path, mode, &handle](const Completion& done)
            {
                subject.Open(path, mode, [done, &handle](fs::Result result, FileHandle file)
                    {
                        handle = file;
                        done(result);
                    });
            });

        if (result != fs::Result::success)
            return std::nullopt;

        return handle;
    }

    std::size_t Runner::Read(FileHandle file, infra::ByteRange buffer)
    {
        std::size_t bytesRead = 0;
        auto recordsBefore = records.size();
        Call(Operation::read, buffer.size(), subject.Size(file), [this, file, buffer, &bytesRead](const Completion& done)
            {
                subject.Read(file, buffer, [done, &bytesRead](fs::Result result, std::size_t size)
                    {
                        bytesRead = size;
                        done(result);
                    });
            });

        if (records.size() != recordsBefore)
            records.back().userBytes = bytesRead;

        return bytesRead;
    }

    fs::Result Runner::Write(FileHandle file, infra::ConstByteRange data)
    {
        return Call(Operation::write, data.size(), subject.Size(file), [this, file, data](const Completion& done)
            {
                subject.Write(file, data, done);
            });
    }

    void Runner::Seek(FileHandle file, uint32_t position)
    {
        if (interrupted)
            return;

        auto startTime = time.Now();
        auto deviceBefore = device.Snapshot();
        auto fileSizeBefore = subject.Size(file);
        subject.Seek(file, position);
        records.push_back({ Operation::seek, phase, fs::Result::success, 0, fileSizeBefore, time.Now() - startTime, device.Snapshot() - deviceBefore });
    }

    uint32_t Runner::Size(FileHandle file) const
    {
        return subject.Size(file);
    }

    fs::Result Runner::Sync(FileHandle file)
    {
        return Call(Operation::sync, 0, subject.Size(file), [this, file](const Completion& done)
            {
                subject.Sync(file, done);
            });
    }

    fs::Result Runner::Close(FileHandle file)
    {
        return Call(Operation::close, 0, subject.Size(file), [this, file](const Completion& done)
            {
                subject.Close(file, done);
            });
    }

    fs::Result Runner::ExpectSize(FileHandle file, uint32_t bytes)
    {
        return Call(Operation::expectSize, 0, subject.Size(file), [this, file, bytes](const Completion& done)
            {
                subject.ExpectSize(file, bytes, done);
            });
    }

    fs::Result Runner::Remove(std::string_view path)
    {
        return Call(Operation::remove, 0, 0, [this, path](const Completion& done)
            {
                subject.Remove(path, done);
            });
    }

    fs::Result Runner::MakeDirectory(std::string_view path)
    {
        return Call(Operation::makeDirectory, 0, 0, [this, path](const Completion& done)
            {
                subject.MakeDirectory(path, done);
            });
    }

    std::optional<std::vector<DirectoryEntry>> Runner::List(std::string_view path)
    {
        std::vector<DirectoryEntry> entries;
        auto result = Call(Operation::list, 0, 0, [this, path, &entries](const Completion& done)
            {
                subject.List(path, [done, &entries](fs::Result result, std::vector<DirectoryEntry> listed)
                    {
                        entries = std::move(listed);
                        done(result);
                    });
            });

        if (result != fs::Result::success)
            return std::nullopt;

        return entries;
    }

    std::optional<VolumeUsage> Runner::QueryUsage()
    {
        VolumeUsage usage{};
        auto result = Call(Operation::usage, 0, 0, [this, &usage](const Completion& done)
            {
                subject.QueryUsage([done, &usage](fs::Result result, VolumeUsage reported)
                    {
                        usage = reported;
                        done(result);
                    });
            });

        if (result != fs::Result::success)
            return std::nullopt;

        return usage;
    }

    void Runner::Idle(infra::Duration duration)
    {
        if (interrupted)
            return;

        auto startTime = time.Now();
        auto deviceBefore = device.Snapshot();
        auto countersBefore = subject.Counters();
        bool hinted = false;

        subject.IdleHint([&hinted]
            {
                hinted = true;
            });
        time.RunUntil([&hinted]
            {
                return hinted;
            },
            duration);

        if (time.Now() < startTime + duration)
            time.RunFor(startTime + duration - time.Now());

        RecordIdle(startTime, deviceBefore, countersBefore);
    }

    bool Runner::Settle(infra::Duration limit)
    {
        if (interrupted)
            return false;

        auto startTime = time.Now();
        auto deviceBefore = device.Snapshot();
        auto countersBefore = subject.Counters();
        auto idle = time.RunUntilIdle(limit);
        RecordIdle(startTime, deviceBefore, countersBefore);
        return idle;
    }

    bool Runner::Interrupted() const
    {
        return interrupted;
    }

    const std::vector<CallRecord>& Runner::Records() const
    {
        return records;
    }

    const DeviceProbe& Runner::Device() const
    {
        return device;
    }

    FileSystemUnderTest& Runner::Subject() const
    {
        return subject;
    }

    fs::Result Runner::Call(Operation operation, uint64_t userBytes, uint32_t fileSizeBefore, const std::function<void(const Completion& done)>& start)
    {
        if (interrupted)
            return fs::Result::deviceError;

        auto startTime = time.Now();
        auto deviceBefore = device.Snapshot();
        auto countersBefore = subject.Counters();
        std::optional<fs::Result> result;

        start([&result](fs::Result completed)
            {
                result = completed;
            });
        really_assert(!result);

        if (!time.RunUntil([&result]
                {
                    return result.has_value();
                },
                callLimit))
        {
            interrupted = true;
            return fs::Result::deviceError;
        }

        records.push_back({ operation, phase, *result, userBytes, fileSizeBefore, time.Now() - startTime, device.Snapshot() - deviceBefore, CountedSince(countersBefore) });
        return *result;
    }

    void Runner::RecordIdle(infra::TimePoint startTime, const DeviceSnapshot& deviceBefore, const std::optional<VolumeCounters>& countersBefore)
    {
        records.push_back({ Operation::idle, phase, fs::Result::success, 0, 0, time.Now() - startTime, device.Snapshot() - deviceBefore, CountedSince(countersBefore) });
    }

    std::optional<VolumeCounters> Runner::CountedSince(const std::optional<VolumeCounters>& countersBefore) const
    {
        auto countersAfter = subject.Counters();
        if (!countersBefore.has_value() || !countersAfter.has_value())
            return std::nullopt;

        return *countersAfter - *countersBefore;
    }
}
