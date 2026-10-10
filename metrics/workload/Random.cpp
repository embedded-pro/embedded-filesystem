#include "metrics/workload/Random.hpp"

namespace metrics
{
    Random::Random(uint64_t seed)
        : state(seed)
    {}

    uint64_t Random::Next()
    {
        state += 0x9e3779b97f4a7c15;
        uint64_t value = state;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9;
        value = (value ^ (value >> 27)) * 0x94d049bb133111eb;
        return value ^ (value >> 31);
    }

    uint32_t Random::Below(uint32_t bound)
    {
        return static_cast<uint32_t>(((Next() >> 32) * bound) >> 32);
    }
}
