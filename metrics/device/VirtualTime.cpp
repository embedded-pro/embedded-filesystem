#include "metrics/device/VirtualTime.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace metrics
{
    class VirtualTime::Running
    {
    public:
        explicit Running(bool& running)
            : running(running)
        {
            really_assert(!running);
            running = true;
        }

        Running(const Running& other) = delete;
        Running& operator=(const Running& other) = delete;

        ~Running()
        {
            running = false;
        }

    private:
        bool& running;
    };

    VirtualTime::VirtualTime(infra::PerfectTimerService& timerService)
        : timerService(timerService)
    {}

    infra::TimePoint VirtualTime::Now() const
    {
        return timerService.Now();
    }

    bool VirtualTime::RunUntil(const std::function<bool()>& predicate, std::optional<infra::Duration> limit)
    {
        Running guard{ running };
        std::optional<infra::TimePoint> deadline;
        if (limit)
            deadline = Now() + *limit;

        while (true)
        {
            Drain();
            if (predicate())
                return true;

            auto next = timerService.NextTrigger();
            if (deadline && next > *deadline)
            {
                AdvanceTo(*deadline);
                Drain();
                return predicate();
            }

            if (next == infra::TimePoint::max())
                return false;

            AdvanceTo(next);
        }
    }

    void VirtualTime::RunFor(infra::Duration duration)
    {
        Running guard{ running };
        auto deadline = Now() + duration;

        do
        {
            Drain();
            AdvanceTo(std::min(deadline, timerService.NextTrigger()));
            Drain();
        } while (Now() != deadline);
    }

    bool VirtualTime::RunUntilIdle(std::optional<infra::Duration> limit)
    {
        Running guard{ running };
        std::optional<infra::TimePoint> deadline;
        if (limit)
            deadline = Now() + *limit;

        while (true)
        {
            Drain();
            auto next = timerService.NextTrigger();
            if (next == infra::TimePoint::max())
                return true;

            if (deadline && next > *deadline)
            {
                AdvanceTo(*deadline);
                Drain();
                return false;
            }

            AdvanceTo(next);
        }
    }

    void VirtualTime::Drain() const
    {
        auto& dispatcher = infra::EventDispatcher::Instance();
        while (!dispatcher.IsIdle())
            dispatcher.ExecuteFirstAction();
    }

    void VirtualTime::AdvanceTo(infra::TimePoint time)
    {
        timerService.TimeProgressed(std::max(time - Now(), infra::Duration{}));
    }
}
