#ifndef METRICS_DEVICE_VIRTUAL_TIME_HPP
#define METRICS_DEVICE_VIRTUAL_TIME_HPP

#include "infra/timer/test_helper/PerfectTimerService.hpp"
#include <functional>
#include <optional>

namespace metrics
{
    class VirtualTime
    {
    public:
        explicit VirtualTime(infra::PerfectTimerService& timerService);
        VirtualTime(const VirtualTime& other) = delete;
        VirtualTime& operator=(const VirtualTime& other) = delete;

        infra::TimePoint Now() const;
        bool RunUntil(const std::function<bool()>& predicate, std::optional<infra::Duration> limit = std::nullopt);
        void RunFor(infra::Duration duration);
        bool RunUntilIdle(std::optional<infra::Duration> limit = std::nullopt);

    private:
        class Running;

        void Drain() const;
        void AdvanceTo(infra::TimePoint time);

    private:
        infra::PerfectTimerService& timerService;
        bool running{ false };
    };
}

#endif
