#include "validation/firmware/workload/WorkloadReport.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "validation/firmware/workload/CountRecords.hpp"

namespace validation
{
    WorkloadReport::WorkloadReport(ResultLines& results, const CycleCounter& cycles, uint32_t clockHz)
        : results(results)
        , phaseStopwatch(cycles, clockHz)
    {}

    void WorkloadReport::Start(const WorkloadContext& context, const FlashCounts& device, const std::optional<VolumeCounts>& volume)
    {
        this->context = context;
        failure.reset();
        window = PhaseWindow::none;
        firstLog.Clear();
        secondLog.Clear();
        phase.Start("", device, volume);

        auto record = results.Record("workload");
        AddRun(record);
        record.Add("scale", context.scale)
            .Add("ramBytes", context.ramBytes);
    }

    void WorkloadReport::StartPhase(infra::BoundedConstString name, PhaseWindow window, const FlashCounts& device, const std::optional<VolumeCounts>& volume)
    {
        PrintPhase(device, volume);
        this->window = window;
        phase.Start(name, device, volume);
        phaseStopwatch.Start();
    }

    void WorkloadReport::Add(const CallStart& start, const CallEnd& end)
    {
        phase.Add(start.operation, end.userBytes, end.latencyNs);
        AddToWindow(start, end);
        Check(start.operation, end.result);
    }

    void WorkloadReport::TimedOut(WorkloadOperation operation)
    {
        Fail(operation).result = "timeout";
        PrintFailure();
    }

    void WorkloadReport::End(const FlashCounts& device, const std::optional<VolumeCounts>& volume)
    {
        PrintPhase(device, volume);
        PrintWindow("first", firstLog.Window());
        PrintWindow("second", secondLog.Window());
        PrintFailure();

        auto record = results.Record("workload.done");
        AddRun(record);
    }

    bool WorkloadReport::Failed() const
    {
        return failure.has_value();
    }

    void WorkloadReport::AddToWindow(const CallStart& start, const CallEnd& end)
    {
        if (window == PhaseWindow::none || !IsPartOfAppend(start.operation) || !start.volume || !end.volume)
            return;

        AppendCall append{ start.operation, start.fileSizeBefore, end.device - start.device, *end.volume - *start.volume };
        if (window == PhaseWindow::firstLog)
            firstLog.Add(append);
        else
            secondLog.Add(append);
    }

    void WorkloadReport::Check(WorkloadOperation operation, fs::Result result)
    {
        auto expected = result == fs::Result::success || (operation == WorkloadOperation::expectSize && result == fs::Result::unsupported);
        if (expected || failure)
            return;

        infra::StringOutputStream stream{ Fail(operation).result };
        stream << result;
    }

    WorkloadReport::Failure& WorkloadReport::Fail(WorkloadOperation operation)
    {
        failure.emplace();
        failure->phase = phase.Name();
        failure->operation = operation;
        return *failure;
    }

    void WorkloadReport::PrintPhase(const FlashCounts& device, const std::optional<VolumeCounts>& volume)
    {
        if (!phase.Name().empty())
            phase.Print(results, context, { phaseStopwatch.ElapsedNs(), device, volume });
    }

    void WorkloadReport::PrintWindow(const char* log, const std::optional<WindowTotals>& totals) const
    {
        if (!totals)
            return;

        auto record = results.Record("window");
        AddRun(record);
        record.Add("log", log)
            .Add("appends", totals->appends);
        AddCounts(record, totals->device);
        AddCounts(record, totals->volume);
    }

    void WorkloadReport::PrintFailure() const
    {
        if (!failure)
            return;

        auto record = results.Record("workload.failed");
        AddRun(record);
        record.Add("phase", failure->phase)
            .Add("op", OperationName(failure->operation))
            .Add("result", failure->result);
    }

    void WorkloadReport::AddRun(ResultRecord& record) const
    {
        record.Add("device", context.device)
            .Add("subject", context.subject)
            .Add("workload", context.workload)
            .Add("fill", context.fill);
    }
}
