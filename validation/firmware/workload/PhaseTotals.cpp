#include "validation/firmware/workload/PhaseTotals.hpp"
#include "validation/firmware/workload/CountRecords.hpp"
#include <algorithm>

namespace validation
{
    void PhaseTotals::Start(infra::BoundedConstString name, const DeviceCounts& device, const std::optional<VolumeCounts>& volume)
    {
        this->name = name;
        deviceAtStart = device;
        volumeAtStart = volume;
        for (auto& operation : operations)
            operation = OperationTotals{};
    }

    void PhaseTotals::Add(WorkloadOperation operation, uint64_t userBytes, uint64_t latencyNs)
    {
        auto& totals = operations[static_cast<std::size_t>(operation)];
        ++totals.calls;
        totals.userBytes += userBytes;
        totals.latencyNs += latencyNs;
        totals.maxNs = std::max(totals.maxNs, latencyNs);
        totals.histogram.Add(latencyNs);
    }

    infra::BoundedConstString PhaseTotals::Name() const
    {
        return name;
    }

    const OperationTotals& PhaseTotals::Of(WorkloadOperation operation) const
    {
        return operations[static_cast<std::size_t>(operation)];
    }

    void PhaseTotals::Print(ResultLines& results, const WorkloadContext& context, const PhaseEnd& end)
    {
        auto record = results.Record("phase");
        record.Add("device", context.device)
            .Add("subject", context.subject)
            .Add("workload", context.workload)
            .Add("phase", name)
            .Add("fill", context.fill)
            .Add("ramBytes", context.ramBytes)
            .Add("elapsedNs", end.elapsedNs);
        AddCounts(record, end.device - deviceAtStart, context.kind);
        if (end.volume && volumeAtStart)
            AddCounts(record, *end.volume - *volumeAtStart);

        for (std::size_t operation = 0; operation != workloadOperations; ++operation)
            if (operations[operation].calls != 0)
                PrintOperation(record, static_cast<WorkloadOperation>(operation));
    }

    void PhaseTotals::PrintOperation(ResultRecord& record, WorkloadOperation operation)
    {
        const auto& totals = Of(operation);
        record.Add(Key(operation, ".calls"), totals.calls);
        record.Add(Key(operation, ".userBytes"), totals.userBytes);
        record.Add(Key(operation, ".latencyNs"), totals.latencyNs);
        record.Add(Key(operation, ".maxNs"), totals.maxNs);
        totals.histogram.NonEmpty(pairs);
        record.Add(Key(operation, ".histogram"), infra::MakeRange(pairs));
    }

    infra::BoundedConstString PhaseTotals::Key(WorkloadOperation operation, const char* suffix)
    {
        key = OperationName(operation);
        key.append(suffix);
        return key;
    }
}
