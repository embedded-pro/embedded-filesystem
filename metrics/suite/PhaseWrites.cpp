#include "metrics/suite/PhaseWrites.hpp"

namespace metrics
{
    namespace
    {
        double Quotient(double numerator, double denominator)
        {
            return denominator == 0 ? 0 : numerator / denominator;
        }
    }

    double WritesOfPhase::MeanBlocksPerWrite() const
    {
        return Quotient(static_cast<double>(blocksWritten), static_cast<double>(writeRequests));
    }

    double WritesOfPhase::AlignedShare() const
    {
        return Quotient(static_cast<double>(alignedWrites), static_cast<double>(writeRequests));
    }

    double WritesOfPhase::AllocationUnitsPerMebibyte(uint64_t userBytes) const
    {
        return Quotient(static_cast<double>(allocationUnits.size()), static_cast<double>(userBytes) / (1024 * 1024));
    }

    PhaseWrites::PhaseWrites(const MeteredBlockDevice& device)
        : device(device)
        , counters(device.Counters())
        , writesPerAllocationUnit(device.WritesPerAllocationUnit())
    {}

    void PhaseWrites::PhaseEnded(const std::string& phase)
    {
        auto delta = device.Counters() - counters;
        auto& writes = phases[phase];
        writes.writeRequests += delta.writeRequests;
        writes.blocksWritten += delta.blocksWritten;
        writes.alignedWrites += delta.alignedWrites;

        const auto& current = device.WritesPerAllocationUnit();
        for (uint32_t unit = 0; unit != current.size(); ++unit)
            if (current[unit] != writesPerAllocationUnit[unit])
                writes.allocationUnits.insert(unit);

        counters = device.Counters();
        writesPerAllocationUnit = current;
    }

    const WritesOfPhase& PhaseWrites::Of(std::string_view phase) const
    {
        static const WritesOfPhase none;
        auto writes = phases.find(phase);
        return writes != phases.end() ? writes->second : none;
    }
}
