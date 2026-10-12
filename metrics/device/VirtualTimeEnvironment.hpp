#ifndef METRICS_DEVICE_VIRTUAL_TIME_ENVIRONMENT_HPP
#define METRICS_DEVICE_VIRTUAL_TIME_ENVIRONMENT_HPP

#include "infra/event/EventDispatcherWithWeakPtr.hpp"
#include "infra/timer/test_helper/PerfectTimerService.hpp"
#include "metrics/device/VirtualTime.hpp"

namespace metrics
{
    class VirtualTimeEnvironment
    {
    public:
        VirtualTimeEnvironment() = default;
        VirtualTimeEnvironment(const VirtualTimeEnvironment& other) = delete;
        VirtualTimeEnvironment& operator=(const VirtualTimeEnvironment& other) = delete;

        VirtualTime& Time();

    private:
        infra::EventDispatcherWithWeakPtr::WithSize<128> eventDispatcher;
        infra::PerfectTimerService timerService{ infra::systemTimerServiceId };
        VirtualTime time{ timerService };
    };
}

#endif
