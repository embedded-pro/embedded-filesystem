#include "fs/nor/NorForegroundLeveling.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <utility>
#include <variant>

namespace fs
{
    NorForegroundLeveling::NorForegroundLeveling(NorVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
        , machine(*this, services::Validated<NorForegroundLeveling>())
    {
        machine.Start<NorLevelingStopped>();
    }

    NorForegroundLeveling::~NorForegroundLeveling() = default;

    void NorForegroundLeveling::Start()
    {
        armed = true;
        machine.Dispatch(NorLevelingStart{});
    }

    void NorForegroundLeveling::Stop()
    {
        really_assert(!std::holds_alternative<NorLevelingMoving>(machine.CurrentState()));
        armed = false;
        machine.Dispatch(NorLevelingStop{});
    }

    void NorForegroundLeveling::Urge()
    {
        armed = true;
    }

    void NorForegroundLeveling::ForegroundClaiming()
    {}

    void NorForegroundLeveling::ForegroundReleased()
    {
        if (std::exchange(armed, false) && MoveDue())
            machine.Dispatch(NorLevelingDue{});
    }

    bool NorForegroundLeveling::MoveDue() const
    {
        const auto& blocks = volume.State().blocks;
        auto free = blocks.Count(NorBlockState::freeErased) + blocks.Count(NorBlockState::freeNotErased);
        return free > 2 * volume.Geometry().NodeBlocksPerCheckpoint() && volume.Leveler().Due();
    }

    void NorForegroundLeveling::ClaimAhead()
    {
        claimer.ClaimUrgent([this]
            {
                machine.Dispatch(NorLevelingGranted{});
            });
    }

    void NorForegroundLeveling::Move()
    {
        volume.Leveler().Move([this]
            {
                machine.Dispatch(NorLevelingMoved{});
            });
    }

    void NorForegroundLeveling::Release()
    {
        claimer.Release();
    }
}
