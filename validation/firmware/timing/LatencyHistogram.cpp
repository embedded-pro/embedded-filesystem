#include "validation/firmware/timing/LatencyHistogram.hpp"
#include <algorithm>
#include <bit>

namespace validation
{
    std::size_t LatencyHistogram::Bucket(uint64_t nanoseconds)
    {
        if (nanoseconds < 4)
            return static_cast<std::size_t>(nanoseconds);

        auto octave = static_cast<std::size_t>(std::bit_width(nanoseconds) - 1);
        auto quarter = static_cast<std::size_t>((nanoseconds >> (octave - 2)) & 3);
        return std::min(4 * (octave - 1) + quarter, buckets - 1);
    }

    void LatencyHistogram::Clear()
    {
        counts.fill(0);
    }

    void LatencyHistogram::Add(uint64_t nanoseconds)
    {
        ++counts[Bucket(nanoseconds)];
    }

    uint32_t LatencyHistogram::Count(std::size_t bucket) const
    {
        return counts[bucket];
    }

    void LatencyHistogram::NonEmpty(infra::BoundedVector<uint32_t>& pairs) const
    {
        pairs.clear();
        for (std::size_t bucket = 0; bucket != buckets; ++bucket)
            if (counts[bucket] != 0)
            {
                pairs.push_back(static_cast<uint32_t>(bucket));
                pairs.push_back(counts[bucket]);
            }
    }
}
