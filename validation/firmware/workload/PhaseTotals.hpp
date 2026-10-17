#ifndef VALIDATION_FIRMWARE_WORKLOAD_PHASE_TOTALS_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_PHASE_TOTALS_HPP

#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/timing/LatencyHistogram.hpp"
#include "validation/firmware/workload/WorkloadOperation.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    struct WorkloadContext
    {
        infra::BoundedConstString device;
        infra::BoundedConstString subject;
        infra::BoundedConstString workload;
        uint32_t fill;
        uint32_t scale;
        std::size_t ramBytes;
    };

    struct OperationTotals
    {
        uint32_t calls{ 0 };
        uint64_t userBytes{ 0 };
        uint64_t latencyNs{ 0 };
        uint64_t maxNs{ 0 };
        LatencyHistogram histogram;
    };

    struct PhaseEnd
    {
        uint64_t elapsedNs;
        FlashCounts device;
        std::optional<VolumeCounts> volume;
    };

    class PhaseTotals
    {
    public:
        static constexpr std::size_t maximumName = 32;

        void Start(infra::BoundedConstString name, const FlashCounts& device, const std::optional<VolumeCounts>& volume);
        void Add(WorkloadOperation operation, uint64_t userBytes, uint64_t latencyNs);
        infra::BoundedConstString Name() const;
        const OperationTotals& Of(WorkloadOperation operation) const;
        void Print(ResultLines& results, const WorkloadContext& context, const PhaseEnd& end);

    private:
        void PrintOperation(ResultRecord& record, WorkloadOperation operation);
        infra::BoundedConstString Key(WorkloadOperation operation, const char* suffix);

    private:
        infra::BoundedString::WithStorage<maximumName> name;
        FlashCounts deviceAtStart;
        std::optional<VolumeCounts> volumeAtStart;
        std::array<OperationTotals, workloadOperations> operations;
        infra::BoundedVector<uint32_t>::WithMaxSize<2 * LatencyHistogram::buckets> pairs;
        infra::BoundedString::WithStorage<24> key;
    };
}

#endif
