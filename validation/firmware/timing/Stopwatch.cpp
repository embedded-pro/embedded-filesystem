#include "validation/firmware/timing/Stopwatch.hpp"
#include "validation/firmware/timing/Nanoseconds.hpp"
#include <chrono>

namespace validation
{
    Stopwatch::Stopwatch(const CycleCounter& cycles, uint32_t clockHz)
        : cycles(cycles)
        , clockHz(clockHz)
    {}

    void Stopwatch::Start()
    {
        startTime = infra::Now();
        startCycles = cycles.Cycles();
    }

    uint64_t Stopwatch::ElapsedNs() const
    {
        auto elapsed = infra::Now() - startTime;
        if (elapsed < std::chrono::seconds(1))
            return Nanoseconds(cycles.Cycles() - startCycles, clockHz);

        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }
}
