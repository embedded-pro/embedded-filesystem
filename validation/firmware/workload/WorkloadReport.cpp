#include "validation/firmware/workload/WorkloadReport.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "validation/firmware/workload/CountRecords.hpp"

namespace validation
{
    WorkloadReport::WorkloadReport(ResultLines& results, const CycleCounter& cycles, uint32_t clockHz)
        : results(results)
        , phaseStopwatch(cycles, clockHz)
    {}

    void WorkloadReport::Start(const WorkloadContext& context, const DeviceCounts& device, const std::optional<VolumeCounts>& volume)
    {
        this->context = context;
        failure.reset();
        window = PhaseWindow::none;
        checkpointed = volume.has_value();
        firstLog.Clear();
        secondLog.Clear();
        firstAppends.Start(0);
        secondAppends.Start(0);
        phase.Start("", device, volume);

        auto record = results.Record("workload");
        AddRun(record);
        record.Add("scale", context.scale)
            .Add("ramBytes", context.ramBytes);
    }

    void WorkloadReport::StartPhase(infra::BoundedConstString name, PhaseWindow window, uint32_t windowFrom, const DeviceCounts& device, const std::optional<VolumeCounts>& volume)
    {
        PrintPhase(device, volume);
        this->window = window;
        if (window == PhaseWindow::firstLog)
            firstAppends.Start(windowFrom);
        else if (window == PhaseWindow::secondLog)
            secondAppends.Start(windowFrom);
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

    void WorkloadReport::End(const DeviceCounts& device, const std::optional<VolumeCounts>& volume)
    {
        PrintPhase(device, volume);
        PrintWindow("first", checkpointed ? firstLog.Window() : firstAppends.Window());
        PrintWindow("second", checkpointed ? secondLog.Window() : secondAppends.Window());
        PrintFailure();

        auto record = results.Record("workload.done");
        AddRun(record);
    }

    bool WorkloadReport::Failed() const
    {
        return failure.has_value();
    }

    // A volume with checkpoints takes its windows by checkpoint periods, and one without by appends, as on the host
    void WorkloadReport::AddToWindow(const CallStart& start, const CallEnd& end)
    {
        if (window == PhaseWindow::none || !IsPartOfAppend(start.operation))
            return;

        AppendCall append{ start.operation, start.fileSizeBefore, end.device - start.device, end.volume.value_or(VolumeCounts{}) - start.volume.value_or(VolumeCounts{}) };
        if (checkpointed && window == PhaseWindow::firstLog)
            firstLog.Add(append);
        else if (checkpointed)
            secondLog.Add(append);
        else if (window == PhaseWindow::firstLog)
            firstAppends.Add(append);
        else
            secondAppends.Add(append);
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

    void WorkloadReport::PrintPhase(const DeviceCounts& device, const std::optional<VolumeCounts>& volume)
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
        AddCounts(record, totals->device, context.kind);
        if (checkpointed)
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
