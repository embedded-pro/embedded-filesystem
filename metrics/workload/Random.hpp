#ifndef METRICS_WORKLOAD_RANDOM_HPP
#define METRICS_WORKLOAD_RANDOM_HPP

#include <cstdint>

namespace metrics
{
    class Random
    {
    public:
        explicit Random(uint64_t seed);

        uint64_t Next();
        uint32_t Below(uint32_t bound);

    private:
        uint64_t state;
    };
}

#endif
