#include "fs/common/OperationQueue.hpp"

namespace fs
{
    OperationQueue::OperationQueue(infra::Duration idleDelay, const infra::Function<void()>& onIdle)
        : idleDelay(idleDelay)
        , onIdle(onIdle)
    {}

    void OperationQueue::ForegroundClaiming()
    {
        idleTimer.Cancel();
    }

    void OperationQueue::ForegroundReleased()
    {
        if (!resource.ClaimsPending())
            idleTimer.Start(idleDelay, [this]
                {
                    onIdle();
                });
    }

    OperationQueue::Claimer::Claimer(OperationQueue& queue)
        : queue(queue)
        , claimer(queue.resource)
    {}

    void OperationQueue::Claimer::Claim(const infra::Function<void()>& onGranted)
    {
        queue.ForegroundClaiming();
        claimer.Claim(onGranted);
    }

    void OperationQueue::Claimer::Release()
    {
        auto wasClaimed = claimer.IsClaimed();

        claimer.Release();

        if (wasClaimed)
            queue.ForegroundReleased();
    }

    bool OperationQueue::Claimer::IsClaimed() const
    {
        return claimer.IsClaimed();
    }

    bool OperationQueue::Claimer::IsQueued() const
    {
        return claimer.IsQueued();
    }

    OperationQueue::BackgroundClaimer::BackgroundClaimer(OperationQueue& queue)
        : queue(queue)
        , claimer(queue.resource)
    {}

    void OperationQueue::BackgroundClaimer::Claim(const infra::Function<void()>& onGranted)
    {
        claimer.Claim(onGranted);
    }

    void OperationQueue::BackgroundClaimer::Release()
    {
        claimer.Release();
    }

    bool OperationQueue::BackgroundClaimer::IsClaimed() const
    {
        return claimer.IsClaimed();
    }

    bool OperationQueue::BackgroundClaimer::ShouldYield() const
    {
        return queue.resource.ClaimsPending();
    }
}
