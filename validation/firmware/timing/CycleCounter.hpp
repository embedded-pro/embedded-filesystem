#ifndef VALIDATION_FIRMWARE_CYCLES_CYCLE_COUNTER_HPP
#define VALIDATION_FIRMWARE_CYCLES_CYCLE_COUNTER_HPP

#include <cstdint>

namespace validation
{
    class CycleCounter
    {
    protected:
        CycleCounter() = default;
        CycleCounter(const CycleCounter& other) = delete;
        CycleCounter& operator=(const CycleCounter& other) = delete;
        ~CycleCounter() = default;

    public:
        // Free running; spans are differences, which stay correct across one wrap
        virtual uint32_t Cycles() const = 0;
    };
}

#endif
