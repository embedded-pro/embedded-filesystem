#ifndef METRICS_WORKLOAD_RUNNER_HPP
#define METRICS_WORKLOAD_RUNNER_HPP

#include "metrics/device/VirtualTime.hpp"
#include "metrics/workload/DeviceProbe.hpp"
#include "metrics/workload/FileSystemUnderTest.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace metrics
{
    enum class Operation : uint8_t
    {
        format,
        mount,
        unmount,
        open,
        read,
        write,
        seek,
        sync,
        close,
        expectSize,
        remove,
        makeDirectory,
        list,
        usage,
        idle
    };

    std::string_view OperationName(Operation operation);

    struct CallRecord
    {
        Operation operation;
        std::string phase;
        fs::Result result;
        uint64_t userBytes;
        uint32_t fileSizeBefore;
        infra::Duration latency;
        DeviceSnapshot device;
    };

    class Runner
    {
    public:
        Runner(VirtualTime& time, FileSystemUnderTest& subject, const DeviceProbe& device);

        using PhaseObserver = std::function<void(const std::string& endedPhase)>;

        void SetPhase(std::string phase);
        void ObservePhases(PhaseObserver observer);

        fs::Result Format();
        fs::Result Mount();
        fs::Result Unmount();
        std::optional<FileHandle> Open(std::string_view path, OpenMode mode);
        std::size_t Read(FileHandle file, infra::ByteRange buffer);
        fs::Result Write(FileHandle file, infra::ConstByteRange data);
        void Seek(FileHandle file, uint32_t position);
        uint32_t Size(FileHandle file) const;
        fs::Result Sync(FileHandle file);
        fs::Result Close(FileHandle file);
        fs::Result ExpectSize(FileHandle file, uint32_t bytes);
        fs::Result Remove(std::string_view path);
        fs::Result MakeDirectory(std::string_view path);
        std::optional<std::vector<DirectoryEntry>> List(std::string_view path);
        std::optional<VolumeUsage> QueryUsage();
        void Idle(infra::Duration duration);
        bool Settle(infra::Duration limit);

        bool Interrupted() const;
        const std::vector<CallRecord>& Records() const;
        const DeviceProbe& Device() const;
        FileSystemUnderTest& Subject() const;

    private:
        using Completion = std::function<void(fs::Result result)>;

        fs::Result Call(Operation operation, uint64_t userBytes, uint32_t fileSizeBefore, const std::function<void(const Completion& done)>& start);
        void RecordIdle(infra::TimePoint startTime, const DeviceSnapshot& deviceBefore);

    private:
        VirtualTime& time;
        FileSystemUnderTest& subject;
        const DeviceProbe& device;
        std::string phase;
        PhaseObserver phaseObserver;
        std::vector<CallRecord> records;
        bool interrupted{ false };
    };
}

#endif
