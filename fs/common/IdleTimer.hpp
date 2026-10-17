#ifndef FS_COMMON_IDLE_TIMER_HPP
#define FS_COMMON_IDLE_TIMER_HPP

#include "fs/common/OperationQueue.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class IdleTimer
    {
    public:
        IdleTimer(const OperationQueue::BackgroundClaimer& claimer, infra::Duration delay, const infra::Function<void()>& onIdle);
        IdleTimer(const IdleTimer& other) = delete;
        IdleTimer& operator=(const IdleTimer& other) = delete;
        ~IdleTimer() = default;

        void ForegroundClaiming();
        void ForegroundReleased();
        bool Armed() const;

    private:
        const OperationQueue::BackgroundClaimer& claimer;
        infra::Duration delay;
        infra::Function<void()> onIdle;
        infra::TimerSingleShot timer;
    };
}

#endif
