#ifndef METRICS_WORKLOAD_RANDOM_HPP
#define METRICS_WORKLOAD_RANDOM_HPP

#include <cstdint>

// Header-only, so that the workloads on the boards (docs/Validation.md) draw the same offsets as the host
namespace metrics
{
    class Random
    {
    public:
        explicit Random(uint64_t seed)
            : state(seed)
        {}

        uint64_t Next()
        {
            state += 0x9e3779b97f4a7c15;
            uint64_t value = state;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9;
            value = (value ^ (value >> 27)) * 0x94d049bb133111eb;
            return value ^ (value >> 31);
        }

        uint32_t Below(uint32_t bound)
        {
            return static_cast<uint32_t>(((Next() >> 32) * bound) >> 32);
        }

    private:
        uint64_t state;
    };
}

#endif
