#ifndef FS_COMMON_OPERATION_QUEUE_HPP
#define FS_COMMON_OPERATION_QUEUE_HPP

#include "infra/event/ClaimableResource.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class OperationQueue
    {
    public:
        class Claimer;
        class BackgroundClaimer;

        OperationQueue(infra::Duration idleDelay, const infra::Function<void()>& onIdle);
        OperationQueue(const OperationQueue& other) = delete;
        OperationQueue& operator=(const OperationQueue& other) = delete;
        ~OperationQueue() = default;

    private:
        void ForegroundClaiming();
        void ForegroundReleased();

    private:
        infra::ClaimableResource resource;
        infra::Duration idleDelay;
        infra::Function<void()> onIdle;
        infra::TimerSingleShot idleTimer;
    };

    class OperationQueue::Claimer
    {
    public:
        explicit Claimer(OperationQueue& queue);
        Claimer(const Claimer& other) = delete;
        Claimer& operator=(const Claimer& other) = delete;
        ~Claimer() = default;

        void Claim(const infra::Function<void()>& onGranted);
        void Release();
        bool IsClaimed() const;
        bool IsQueued() const;

    private:
        OperationQueue& queue;
        infra::ClaimableResource::Claimer claimer;
    };

    class OperationQueue::BackgroundClaimer
    {
    public:
        explicit BackgroundClaimer(OperationQueue& queue);
        BackgroundClaimer(const BackgroundClaimer& other) = delete;
        BackgroundClaimer& operator=(const BackgroundClaimer& other) = delete;
        ~BackgroundClaimer() = default;

        void Claim(const infra::Function<void()>& onGranted);
        void Release();
        bool IsClaimed() const;
        bool ShouldYield() const;

    private:
        OperationQueue& queue;
        infra::ClaimableResource::Claimer claimer;
    };
}

#endif
