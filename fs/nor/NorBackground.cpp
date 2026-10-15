#include "fs/nor/NorBackground.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <variant>

namespace fs
{
    NorBackground::NorBackground(NorVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
        , machine(*this, services::Validated<NorBackground>())
    {
        machine.Start<NorBackgroundStopped>();
    }

    NorBackground::~NorBackground() = default;

    void NorBackground::Start()
    {
        urgent = false;
        machine.Dispatch(NorBackgroundStart{});
    }

    void NorBackground::Stop()
    {
        really_assert(!std::holds_alternative<NorBackgroundWorking>(machine.CurrentState()));
        machine.Dispatch(NorBackgroundStop{});
    }

    void NorBackground::Idle()
    {
        machine.Dispatch(NorBackgroundIdle{});
    }

    void NorBackground::Urge()
    {
        if (!claimer.IsClaimed() && volume.Leveler().FallbackDue())
            urgent = true;
    }

    void NorBackground::CallReleased()
    {
        if (FallbackDue())
            machine.Dispatch(NorBackgroundUrge{});
    }

    bool NorBackground::HasWork() const
    {
        return FallbackDue() || CheckpointDue() || volume.Eraser().HasWork() || MoveDue();
    }

    bool NorBackground::FallbackDue() const
    {
        const auto& blocks = volume.State().blocks;
        auto free = blocks.Count(NorBlockState::freeErased) + blocks.Count(NorBlockState::freeNotErased);
        return urgent && volume.Leveler().FallbackDue() && free > 2 * volume.Geometry().NodeBlocksPerCheckpoint();
    }

    bool NorBackground::MoveDue() const
    {
        return PoolHoldsAMove() && volume.Leveler().Due();
    }

    bool NorBackground::PoolHoldsAMove() const
    {
        return volume.State().blocks.Count(NorBlockState::freeErased) > 2 * volume.Geometry().NodeBlocksPerCheckpoint();
    }

    bool NorBackground::CheckpointDue() const
    {
        const auto& cache = volume.Cache();
        auto due = volume.Journal().IsMoreThanHalfFull() || 2 * cache.ModifiedNodes() > cache.Capacity();
        auto nodeBlocks = (volume.Collector().Due() ? 2 : 1) * volume.Geometry().NodeBlocksPerCheckpoint();
        return due && volume.State().blocks.Count(NorBlockState::freeErased) > nodeBlocks;
    }

    bool NorBackground::ShouldStop() const
    {
        return (claimer.ShouldYield() && !FallbackDue()) || !HasWork();
    }

    void NorBackground::Claim()
    {
        claimer.Claim(machine.CompletionWith<void()>([]
            {
                return NorBackgroundGranted{};
            }));
    }

    void NorBackground::ClaimAhead()
    {
        claimer.ClaimUrgent(machine.CompletionWith<void()>([]
            {
                return NorBackgroundGranted{};
            }));
    }

    void NorBackground::Work()
    {
        auto fallback = FallbackDue();
        urgent = false;
        erasing = false;

        if (!fallback && CheckpointDue())
            WriteCheckpoint();
        else if (!fallback && volume.Eraser().HasWork())
        {
            erasing = true;
            volume.Eraser().Step(machine.CompletionWith<void()>([]
                {
                    return NorBackgroundUnitDone{};
                }));
        }
        else
            MoveBlock();
    }

    void NorBackground::WriteCheckpoint()
    {
        volume.Checkpoints().Write(machine.CompletionWith<void(Result result)>([](Result result)
            {
                // The pool holds every block that the checkpoint may take
                really_assert(result == Result::success);
                return NorBackgroundUnitDone{};
            }));
    }

    void NorBackground::MoveBlock()
    {
        volume.Leveler().Move(machine.CompletionWith<void()>([]
            {
                return NorBackgroundUnitDone{};
            }));
    }

    void NorBackground::Release()
    {
        if (erasing && claimer.ShouldYield())
            volume.Eraser().CountAsRefill();

        erasing = false;
        urgent = false;
        claimer.Release();
    }
}
