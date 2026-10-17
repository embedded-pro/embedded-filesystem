#ifndef VALIDATION_FIRMWARE_TIMING_LATENCY_HISTOGRAM_HPP
#define VALIDATION_FIRMWARE_TIMING_LATENCY_HISTOGRAM_HPP

#include "infra/util/BoundedVector.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    // Four buckets per octave of nanoseconds, so that the host merges the histograms of phases into a percentile within 19 %
    class LatencyHistogram
    {
    public:
        static constexpr std::size_t buckets = 164;

        static std::size_t Bucket(uint64_t nanoseconds);

        void Clear();
        void Add(uint64_t nanoseconds);
        uint32_t Count(std::size_t bucket) const;
        void NonEmpty(infra::BoundedVector<uint32_t>& pairs) const;

    private:
        std::array<uint32_t, buckets> counts{};
    };
}

#endif
