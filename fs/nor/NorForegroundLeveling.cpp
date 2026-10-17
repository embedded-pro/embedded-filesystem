#include "fs/nor/NorForegroundLeveling.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <utility>

namespace fs
{
    NorForegroundLeveling::NorForegroundLeveling(NorVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    NorForegroundLeveling::~NorForegroundLeveling() = default;

    void NorForegroundLeveling::Start()
    {
        armed = true;

        if (phase == Phase::stopped)
            phase = Phase::waiting;
    }

    void NorForegroundLeveling::Stop()
    {
        really_assert(phase != Phase::moving);
        armed = false;

        if (phase == Phase::claiming)
            claimer.Release();

        phase = Phase::stopped;
    }

    void NorForegroundLeveling::Urge()
    {
        armed = true;
    }

    void NorForegroundLeveling::ForegroundClaiming()
    {}

    void NorForegroundLeveling::ForegroundReleased()
    {
        if (std::exchange(armed, false) && phase == Phase::waiting && MoveDue())
            ClaimAhead();
    }

    bool NorForegroundLeveling::Scheduled() const
    {
        return false;
    }

    bool NorForegroundLeveling::MoveDue() const
    {
        const auto& blocks = volume.State().blocks;
        auto free = blocks.Count(NorBlockState::freeErased) + blocks.Count(NorBlockState::freeNotErased);
        return free > 2 * volume.Geometry().NodeBlocksPerCheckpoint() && volume.Leveler().Due();
    }

    void NorForegroundLeveling::ClaimAhead()
    {
        phase = Phase::claiming;
        claimer.ClaimUrgent([this]
            {
                Move();
            });
    }

    void NorForegroundLeveling::Move()
    {
        phase = Phase::moving;
        volume.Leveler().Move([this]
            {
                Moved();
            });
    }

    void NorForegroundLeveling::Moved()
    {
        phase = Phase::waiting;
        claimer.Release();
    }
}
