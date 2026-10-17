#ifndef VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_RUNNER_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_RUNNER_HPP

#include "fs/interfaces/Directory.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/interfaces/OpenMode.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/SharedPtr.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include "validation/firmware/timing/Stopwatch.hpp"
#include "validation/firmware/workload/IdleWait.hpp"
#include "validation/firmware/workload/PhaseTotals.hpp"
#include "validation/firmware/workload/WorkloadOperation.hpp"
#include "validation/firmware/workload/WorkloadReport.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    // Runs the calls of a workload one at a time, as metrics::Runner does on the host, but without a record per call: each
    // call is added to the totals of its phase, and the windows of W-2 are kept as the calls come
    class WorkloadRunner
    {
    public:
        static constexpr infra::Duration callLimit = std::chrono::minutes(10);

        using Done = infra::Function<void(fs::Result result)>;

        WorkloadRunner(ResultLines& results, const CycleCounter& cycles, uint32_t clockHz);
        WorkloadRunner(const WorkloadRunner& other) = delete;
        WorkloadRunner& operator=(const WorkloadRunner& other) = delete;

        void Release(const infra::Function<void()>& onReleased);
        void Begin(WorkloadVolume& volume, CountingFlash& flash, const WorkloadContext& context, const infra::Function<void()>& onInterrupted);
        void End();
        void SetPhase(infra::BoundedConstString phase, PhaseWindow window = PhaseWindow::none);

        void Format(const Done& onDone);
        void Mount(const Done& onDone);
        void Unmount(const Done& onDone);
        void Open(infra::BoundedConstString path, fs::OpenMode mode, const Done& onDone);
        void Read(infra::ByteRange buffer, const infra::Function<void(fs::Result result, std::size_t bytesRead)>& onDone);
        void Write(infra::ConstByteRange data, const Done& onDone);
        void Seek(uint32_t position);
        void Sync(const Done& onDone);
        void Close(const Done& onDone);
        void ExpectSize(uint32_t size, const Done& onDone);
        void Remove(infra::BoundedConstString path, const Done& onDone);
        void MakeDirectory(infra::BoundedConstString path, const Done& onDone);
        void List(infra::BoundedConstString path, const Done& onDone);
        void Usage(const infra::Function<void(fs::Result result, fs::VolumeUsage usage)>& onDone);
        void Idle(infra::Duration duration, const infra::Function<void()>& onDone);
        void Settle(infra::Duration limit, const infra::Function<void()>& onDone);

        uint32_t FileSize() const;
        bool Running() const;
        bool Interrupted() const;
        bool Failed() const;

    private:
        enum class State : uint8_t
        {
            idle,
            running,
            interrupted
        };

        void StartCall(WorkloadOperation operation, uint64_t userBytes = 0, bool limited = true);
        void EndCall(fs::Result result, uint64_t userBytes);
        void Completed(fs::Result result);
        void NextEntry();
        void IdleDone();
        void Interrupt();
        std::optional<VolumeCounts> VolumeNow() const;

    private:
        WorkloadReport report;
        Stopwatch callStopwatch;
        State state{ State::idle };

        WorkloadVolume* volume{ nullptr };
        CountingFlash* flash{ nullptr };
        infra::Function<void()> onInterrupted;
        WorkloadVolume* mountedVolume{ nullptr };
        VolumeCounts earlierMounts;
        std::optional<VolumeCounts> countedBeforeMount;

        CallStart call{};
        infra::TimerSingleShot callTimer;
        IdleWait idle{ [this]
            {
                IdleDone();
            } };

        infra::SharedPtr<fs::File> file;
        infra::SharedPtr<fs::Directory> directory;
        infra::AutoResetFunction<void(fs::Result result)> onDone;
        infra::AutoResetFunction<void(fs::Result result, std::size_t bytesRead)> onRead;
        infra::AutoResetFunction<void(fs::Result result, fs::VolumeUsage usage)> onUsage;
        infra::AutoResetFunction<void()> onIdle;
        infra::AutoResetFunction<void()> onReleased;
    };
}

#endif
