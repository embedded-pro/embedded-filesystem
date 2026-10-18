#ifndef VALIDATION_FIRMWARE_DEVICES_REQUEST_METER_HPP
#define VALIDATION_FIRMWARE_DEVICES_REQUEST_METER_HPP

#include "infra/timer/Timer.hpp"
#include "infra/util/Function.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include <cstdint>

namespace validation
{
    // Measures a device request from its start to its completion, and calls the timeout handler when it outlasts its guard
    class RequestMeter
    {
    public:
        RequestMeter(const CycleCounter& cycles, uint32_t clockHz);
        RequestMeter(const RequestMeter& other) = delete;
        RequestMeter& operator=(const RequestMeter& other) = delete;

        void OnTimeout(const infra::Function<void()>& onTimeout);
        bool TimeoutWatched() const;

        void Start(uint64_t& totalNs, infra::Duration guard);
        void Stop();
        uint32_t LastCycles() const;

    private:
        const CycleCounter& cycles;
        uint32_t clockHz;
        infra::Function<void()> onTimeout;
        infra::TimerSingleShot guardTimer;
        uint64_t* requestNs{ nullptr };
        uint32_t startCycles{ 0 };
        uint32_t lastCycles{ 0 };
    };
}

#endif
