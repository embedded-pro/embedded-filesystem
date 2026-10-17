#include "fs/common/OperationQueue.hpp"

namespace fs
{
    OperationQueue::OperationQueue(const infra::Function<void()>& onForegroundClaiming, const infra::Function<void()>& onForegroundReleased)
        : onForegroundClaiming(onForegroundClaiming)
        , onForegroundReleased(onForegroundReleased)
    {}

    uint32_t OperationQueue::ForegroundReleases() const
    {
        return foregroundReleases;
    }

    void OperationQueue::ForegroundClaiming()
    {
        if (onForegroundClaiming)
            onForegroundClaiming();
    }

    void OperationQueue::ForegroundReleased()
    {
        ++foregroundReleases;

        if (onForegroundReleased)
            onForegroundReleased();
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

    void OperationQueue::BackgroundClaimer::ClaimUrgent(const infra::Function<void()>& onGranted)
    {
        claimer.ClaimUrgent(onGranted);
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
