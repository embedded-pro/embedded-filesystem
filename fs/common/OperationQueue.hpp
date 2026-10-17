#ifndef FS_COMMON_OPERATION_QUEUE_HPP
#define FS_COMMON_OPERATION_QUEUE_HPP

#include "infra/event/ClaimableResource.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class OperationQueue
    {
    public:
        class Claimer;
        class BackgroundClaimer;

        explicit OperationQueue(const infra::Function<void()>& onForegroundClaiming = nullptr, const infra::Function<void()>& onForegroundReleased = nullptr);
        OperationQueue(const OperationQueue& other) = delete;
        OperationQueue& operator=(const OperationQueue& other) = delete;
        ~OperationQueue() = default;

        uint32_t ForegroundReleases() const;

    private:
        void ForegroundClaiming();
        void ForegroundReleased();

    private:
        infra::ClaimableResource resource;
        infra::Function<void()> onForegroundClaiming;
        infra::Function<void()> onForegroundReleased;
        uint32_t foregroundReleases{ 0 };
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
