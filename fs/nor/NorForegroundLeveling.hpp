#ifndef FS_NOR_NOR_FOREGROUND_LEVELING_HPP
#define FS_NOR_NOR_FOREGROUND_LEVELING_HPP

#include "fs/common/OperationQueue.hpp"
#include "fs/nor/NorBackgroundWork.hpp"
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorForegroundLeveling
        : public NorBackgroundWork
    {
    public:
        explicit NorForegroundLeveling(NorVolume& volume);
        NorForegroundLeveling(const NorForegroundLeveling& other) = delete;
        NorForegroundLeveling& operator=(const NorForegroundLeveling& other) = delete;
        ~NorForegroundLeveling();

        void Start() override;
        void Stop() override;
        void Urge() override;
        void ForegroundClaiming() override;
        void ForegroundReleased() override;
        bool Scheduled() const override;

    private:
        enum class Phase : uint8_t
        {
            stopped,
            waiting,
            claiming,
            moving
        };

        bool MoveDue() const;
        void ClaimAhead();
        void Move();
        void Moved();

    private:
        NorVolume& volume;
        OperationQueue::BackgroundClaimer claimer;
        bool armed{ false };
        Phase phase{ Phase::stopped };
    };
}

#endif
