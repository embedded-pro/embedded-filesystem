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

    bool OperationQueue::Idle() const
    {
        return outstandingClaims == 0;
    }

    void OperationQueue::ForegroundClaiming()
    {
        if (onForegroundClaiming)
            onForegroundClaiming();
    }

    void OperationQueue::Withdrawn()
    {
        --outstandingClaims;
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

    OperationQueue::Claimer::~Claimer()
    {
        if (claimer.IsClaimed())
            queue.Withdrawn();

        if (claimer.IsQueued())
            queue.Withdrawn();
    }

    void OperationQueue::Claimer::Claim(const infra::Function<void()>& onGranted)
    {
        queue.ForegroundClaiming();
        ++queue.outstandingClaims;
        claimer.Claim(onGranted);
    }

    void OperationQueue::Claimer::Release()
    {
        auto wasClaimed = claimer.IsClaimed();
        auto wasOutstanding = wasClaimed || claimer.IsQueued();

        claimer.Release();

        if (wasOutstanding)
            queue.Withdrawn();

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

    OperationQueue::BackgroundClaimer::~BackgroundClaimer()
    {
        if (claimer.IsClaimed())
            queue.Withdrawn();

        if (claimer.IsQueued())
            queue.Withdrawn();
    }

    void OperationQueue::BackgroundClaimer::Claim(const infra::Function<void()>& onGranted)
    {
        ++queue.outstandingClaims;
        claimer.Claim(onGranted);
    }

    void OperationQueue::BackgroundClaimer::ClaimUrgent(const infra::Function<void()>& onGranted)
    {
        ++queue.outstandingClaims;
        claimer.ClaimUrgent(onGranted);
    }

    void OperationQueue::BackgroundClaimer::Release()
    {
        auto wasOutstanding = claimer.IsClaimed() || claimer.IsQueued();

        claimer.Release();

        if (wasOutstanding)
            queue.Withdrawn();
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
