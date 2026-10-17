#ifndef FS_COMMON_OPERATION_QUEUE_HPP
#define FS_COMMON_OPERATION_QUEUE_HPP

#include "infra/event/ClaimableResource.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class OperationQueue
    {
    public:
        class Claimer;
        class BackgroundClaimer;

        OperationQueue(infra::Duration idleDelay, const infra::Function<void()>& onIdle, const infra::Function<void()>& onReleased = nullptr);
        OperationQueue(const OperationQueue& other) = delete;
        OperationQueue& operator=(const OperationQueue& other) = delete;
        ~OperationQueue() = default;

        uint32_t ForegroundReleases() const;
        bool Idle() const;

    private:
        void ForegroundClaiming();
        void ForegroundReleased();
        void Withdrawn();

    private:
        infra::ClaimableResource resource;
        infra::Duration idleDelay;
        infra::Function<void()> onIdle;
        infra::Function<void()> onReleased;
        infra::TimerSingleShot idleTimer;
        uint32_t foregroundReleases{ 0 };
        uint32_t outstandingClaims{ 0 };
    };

    class OperationQueue::Claimer
    {
    public:
        explicit Claimer(OperationQueue& queue);
        Claimer(const Claimer& other) = delete;
        Claimer& operator=(const Claimer& other) = delete;
        ~Claimer();

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
        ~BackgroundClaimer();

        void Claim(const infra::Function<void()>& onGranted);
        void ClaimUrgent(const infra::Function<void()>& onGranted);
        void Release();
        bool IsClaimed() const;
        bool ShouldYield() const;

    private:
        OperationQueue& queue;
        infra::ClaimableResource::Claimer claimer;
    };
}

#endif
