#ifndef METRICS_DEVICE_BLOCK_DEVICE_STATISTICS_HPP
#define METRICS_DEVICE_BLOCK_DEVICE_STATISTICS_HPP

#include "infra/timer/TimerService.hpp"
#include <cstdint>

namespace metrics
{
    struct BlockDeviceCounters
    {
        uint64_t readRequests{};
        uint64_t writeRequests{};
        uint64_t discardRequests{};
        uint64_t flushRequests{};
        uint64_t failedRequests{};
        uint64_t blocksRead{};
        uint64_t blocksWritten{};
        uint64_t blocksDiscarded{};
        uint64_t alignedWrites{};
        uint64_t allocationUnitOpenings{};
        infra::Duration readTime{};
        infra::Duration writeTime{};
        infra::Duration discardTime{};
        infra::Duration flushTime{};

        infra::Duration DeviceTime() const;
        double MeanBlocksPerWrite() const;
        double AlignedShare() const;
        BlockDeviceCounters operator-(const BlockDeviceCounters& earlier) const;
        bool operator==(const BlockDeviceCounters& other) const = default;
    };
}

#endif
