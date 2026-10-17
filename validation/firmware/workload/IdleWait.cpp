#include "validation/firmware/workload/IdleWait.hpp"

namespace validation
{
    IdleWait::IdleWait(const infra::Function<void()>& onDone)
        : onDone(onDone)
    {}

    void IdleWait::Wait(infra::Duration duration)
    {
        timer.Start(duration, [this]
            {
                onDone();
            });
    }

    void IdleWait::Settle(const fs::FileSystem& fileSystem, infra::Duration limit)
    {
        this->fileSystem = &fileSystem;
        deadline = infra::Now() + limit;
        pollTimer.Start(pollInterval, [this]
            {
                Poll();
            });
    }

    void IdleWait::Cancel()
    {
        timer.Cancel();
        pollTimer.Cancel();
    }

    void IdleWait::Poll()
    {
        if (fileSystem->Idle() || infra::Now() >= deadline)
        {
            pollTimer.Cancel();
            onDone();
        }
    }
}
