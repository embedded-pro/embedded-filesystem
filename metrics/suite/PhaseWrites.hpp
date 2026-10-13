#ifndef METRICS_SUITE_PHASE_WRITES_HPP
#define METRICS_SUITE_PHASE_WRITES_HPP

#include "metrics/device/MeteredBlockDevice.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace metrics
{
    struct WritesOfPhase
    {
        uint64_t writeRequests{};
        uint64_t blocksWritten{};
        uint64_t alignedBlocks{};
        std::set<uint32_t> allocationUnits;

        double MeanBlocksPerWrite() const;
        double AlignedShare() const;
        double AllocationUnitsPerMebibyte(uint64_t userBytes) const;
    };

    class PhaseWrites
    {
    public:
        explicit PhaseWrites(const MeteredBlockDevice& device);

        void PhaseEnded(const std::string& phase);
        const WritesOfPhase& Of(std::string_view phase) const;

    private:
        const MeteredBlockDevice& device;
        BlockDeviceCounters counters;
        std::vector<uint64_t> writesPerAllocationUnit;
        std::map<std::string, WritesOfPhase, std::less<>> phases;
    };
}

#endif
