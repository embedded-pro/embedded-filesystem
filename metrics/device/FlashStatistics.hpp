#ifndef METRICS_DEVICE_FLASH_STATISTICS_HPP
#define METRICS_DEVICE_FLASH_STATISTICS_HPP

#include "infra/timer/TimerService.hpp"
#include <cstdint>
#include <vector>

namespace metrics
{
    struct FlashCounters
    {
        uint64_t readRequests{};
        uint64_t programRequests{};
        uint64_t eraseRequests{};
        uint64_t bytesRead{};
        uint64_t bytesProgrammed{};
        uint64_t sectorsErased{};
        uint64_t programUnitRewrites{};
        infra::Duration readTime{};
        infra::Duration programTime{};
        infra::Duration eraseTime{};

        infra::Duration DeviceTime() const;
        FlashCounters operator-(const FlashCounters& earlier) const;
        bool operator==(const FlashCounters& other) const = default;
    };

    struct WearSummary
    {
        uint32_t minimum{};
        uint32_t maximum{};
        double mean{};

        double Spread() const;
        double Efficiency() const;
    };

    WearSummary SummarizeWear(const std::vector<uint32_t>& eraseCounts);
}

#endif
