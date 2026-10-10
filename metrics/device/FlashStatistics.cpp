#include "metrics/device/FlashStatistics.hpp"
#include <algorithm>
#include <numeric>

namespace metrics
{
    infra::Duration FlashCounters::DeviceTime() const
    {
        return readTime + programTime + eraseTime;
    }

    FlashCounters FlashCounters::operator-(const FlashCounters& earlier) const
    {
        return FlashCounters{
            readRequests - earlier.readRequests,
            programRequests - earlier.programRequests,
            eraseRequests - earlier.eraseRequests,
            bytesRead - earlier.bytesRead,
            bytesProgrammed - earlier.bytesProgrammed,
            sectorsErased - earlier.sectorsErased,
            programUnitRewrites - earlier.programUnitRewrites,
            readTime - earlier.readTime,
            programTime - earlier.programTime,
            eraseTime - earlier.eraseTime,
        };
    }

    double WearSummary::Spread() const
    {
        return maximum == 0 ? 1.0 : maximum / mean;
    }

    double WearSummary::Efficiency() const
    {
        return maximum == 0 ? 1.0 : mean / maximum;
    }

    WearSummary SummarizeWear(const std::vector<uint32_t>& eraseCounts)
    {
        if (eraseCounts.empty())
            return {};

        auto [minimum, maximum] = std::minmax_element(eraseCounts.begin(), eraseCounts.end());
        auto total = std::accumulate(eraseCounts.begin(), eraseCounts.end(), uint64_t{});
        return WearSummary{ *minimum, *maximum, static_cast<double>(total) / eraseCounts.size() };
    }
}
