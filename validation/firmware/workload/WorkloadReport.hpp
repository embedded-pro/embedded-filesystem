#ifndef VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_REPORT_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_REPORT_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include "validation/firmware/timing/Stopwatch.hpp"
#include "validation/firmware/workload/AppendWindows.hpp"
#include "validation/firmware/workload/PhaseTotals.hpp"
#include "validation/firmware/workload/WorkloadOperation.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"
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

    struct CallStart
    {
        WorkloadOperation operation;
        uint64_t userBytes;
        uint32_t fileSizeBefore;
        FlashCounts device;
        std::optional<VolumeCounts> volume;
    };

    struct CallEnd
    {
        fs::Result result;
        uint64_t userBytes;
        uint64_t latencyNs;
        FlashCounts device;
        std::optional<VolumeCounts> volume;
    };

    // The records of a run: the workload, the totals of each phase, the windows of W-2, the first failure and the end
    class WorkloadReport
    {
    public:
        static constexpr uint32_t smallLogSize = 4096;

        WorkloadReport(ResultLines& results, const CycleCounter& cycles, uint32_t clockHz);

        void Start(const WorkloadContext& context, const FlashCounts& device, const std::optional<VolumeCounts>& volume);
        void StartPhase(infra::BoundedConstString name, PhaseWindow window, const FlashCounts& device, const std::optional<VolumeCounts>& volume);
        void Add(const CallStart& start, const CallEnd& end);
        void TimedOut(WorkloadOperation operation);
        void End(const FlashCounts& device, const std::optional<VolumeCounts>& volume);
        bool Failed() const;

    private:
        struct Failure
        {
            infra::BoundedString::WithStorage<PhaseTotals::maximumName> phase;
            WorkloadOperation operation;
            infra::BoundedString::WithStorage<20> result;
        };

        void AddToWindow(const CallStart& start, const CallEnd& end);
        void Check(WorkloadOperation operation, fs::Result result);
        Failure& Fail(WorkloadOperation operation);
        void PrintPhase(const FlashCounts& device, const std::optional<VolumeCounts>& volume);
        void PrintWindow(const char* log, const std::optional<WindowTotals>& totals) const;
        void PrintFailure() const;
        void AddRun(ResultRecord& record) const;

    private:
        ResultLines& results;
        Stopwatch phaseStopwatch;
        WorkloadContext context{};
        PhaseTotals phase;
        PhaseWindow window{ PhaseWindow::none };
        LastPeriods firstLog;
        PeriodsFrom secondLog{ smallLogSize };
        std::optional<Failure> failure;
    };
}

#endif
