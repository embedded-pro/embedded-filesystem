#ifndef VALIDATION_FIRMWARE_TIMING_STOPWATCH_HPP
#define VALIDATION_FIRMWARE_TIMING_STOPWATCH_HPP

#include "infra/timer/Timer.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include <cstdint>

namespace validation
{
    // The cycle counter wraps after 10.7 s at 400 MHz and the clock ticks every millisecond, so a span of a second or
    // more is taken from the clock, and a shorter one from the cycle counter
    class Stopwatch
    {
    public:
        Stopwatch(const CycleCounter& cycles, uint32_t clockHz);

        void Start();
        uint64_t ElapsedNs() const;

    private:
        const CycleCounter& cycles;
        uint32_t clockHz;
        infra::TimePoint startTime;
        uint32_t startCycles{ 0 };
    };
}

#endif
