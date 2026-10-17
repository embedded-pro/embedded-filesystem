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
#include "validation/firmware/workload/AppendWindows.hpp"
#include "validation/firmware/workload/PhaseTotals.hpp"
#include "validation/firmware/workload/WorkloadOperation.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    enum class PhaseWindow : uint8_t
    {
        none,
        firstLog,
        secondLog
    };

    // Runs the calls of a workload one at a time, as metrics::Runner does on the host, but without a record per call: each
    // call is added to the totals of its phase, and the windows of W-2 are kept as the calls come
    class WorkloadRunner
    {
    public:
        static constexpr infra::Duration callLimit = std::chrono::minutes(10);
        static constexpr infra::Duration pollInterval = std::chrono::milliseconds(1);
        static constexpr uint32_t smallLogSize = 4096;

        using Done = infra::Function<void(fs::Result result)>;

        WorkloadRunner(ResultLines& results, const CycleCounter& cycles, uint32_t clockHz);
        WorkloadRunner(const WorkloadRunner& other) = delete;
        WorkloadRunner& operator=(const WorkloadRunner& other) = delete;
        ~WorkloadRunner() = default;

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
        struct Call
        {
            WorkloadOperation operation;
            uint64_t userBytes;
            uint32_t fileSizeBefore;
            FlashCounts device;
            std::optional<VolumeCounts> volume;
        };

        struct Failure
        {
            infra::BoundedString::WithStorage<PhaseTotals::maximumName> phase;
            WorkloadOperation operation;
            infra::BoundedString::WithStorage<20> result;
        };

        void StartCall(WorkloadOperation operation, uint64_t userBytes = 0, bool limited = true);
        void EndCall(fs::Result result, uint64_t userBytes);
        void Completed(fs::Result result);
        void NextEntry();
        void PollIdle();
        void IdleDone();
        void AddToWindow();
        void Check(fs::Result result);
        void Interrupt();
        void PrintPhase();
        void PrintWindow(const char* log, const std::optional<WindowTotals>& window);
        void PrintStart() const;
        void PrintFailure() const;
        void PrintDone() const;
        std::optional<VolumeCounts> VolumeNow() const;

    private:
        ResultLines& results;
        Stopwatch callStopwatch;
        Stopwatch phaseStopwatch;

        WorkloadVolume* volume{ nullptr };
        CountingFlash* flash{ nullptr };
        WorkloadContext context{};
        infra::Function<void()> onInterrupted;
        WorkloadVolume* mountedVolume{ nullptr };
        bool running{ false };
        bool interrupted{ false };
        std::optional<Failure> failure;

        PhaseTotals phase;
        PhaseWindow window{ PhaseWindow::none };
        LastPeriods firstLog;
        PeriodsFrom secondLog{ smallLogSize };
        VolumeCounts earlierMounts;

        Call call{};
        infra::TimerSingleShot callTimer;
        infra::TimerSingleShot idleTimer;
        infra::TimerRepeating pollTimer;
        infra::TimePoint settleDeadline;

        infra::SharedPtr<fs::File> file;
        infra::SharedPtr<fs::Directory> directory;
        std::optional<VolumeCounts> countedBeforeMount;
        infra::AutoResetFunction<void(fs::Result result)> onDone;
        infra::AutoResetFunction<void(fs::Result result, std::size_t bytesRead)> onRead;
        infra::AutoResetFunction<void(fs::Result result, fs::VolumeUsage usage)> onUsage;
        infra::AutoResetFunction<void()> onIdle;
        infra::AutoResetFunction<void()> onReleased;
    };
}

#endif
