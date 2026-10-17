#include "fs/common/IdleTimer.hpp"

namespace fs
{
    IdleTimer::IdleTimer(const OperationQueue::BackgroundClaimer& claimer, infra::Duration delay, const infra::Function<void()>& onIdle)
        : claimer(claimer)
        , delay(delay)
        , onIdle(onIdle)
    {}

    void IdleTimer::ForegroundClaiming()
    {
        timer.Cancel();
    }

    void IdleTimer::ForegroundReleased()
    {
        if (!claimer.ShouldYield())
            timer.Start(delay, onIdle);
    }
}
