#ifndef VALIDATION_FIRMWARE_TIMING_TEST_DOUBLES_FAKE_CYCLE_COUNTER_HPP
#define VALIDATION_FIRMWARE_TIMING_TEST_DOUBLES_FAKE_CYCLE_COUNTER_HPP

#include "infra/timer/Timer.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include <chrono>
#include <cstdint>

namespace validation
{
    // Cycles at one per microsecond of the clock of the fixture, plus the cycles a test adds
    class FakeCycleCounter
        : public CycleCounter
    {
    public:
        static constexpr uint32_t clockHz = 1'000'000;

        uint32_t Cycles() const override
        {
            return static_cast<uint32_t>(added + std::chrono::duration_cast<std::chrono::microseconds>(infra::Now() - epoch).count());
        }

        infra::TimePoint epoch{ infra::Now() };
        uint64_t added{ 0 };
    };
}

#endif
