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
        machine.Dispatch(NorBackgroundStart{});
    }

    void NorBackground::Stop()
    {
        really_assert(std::holds_alternative<NorBackgroundWaiting>(machine.CurrentState()) || std::holds_alternative<NorBackgroundStopped>(machine.CurrentState()));
        machine.Dispatch(NorBackgroundStop{});
    }

    void NorBackground::Idle()
    {
        machine.Dispatch(NorBackgroundIdle{});
    }

    bool NorBackground::HasWork() const
    {
        return CheckpointDue() || volume.Eraser().HasWork();
    }

    bool NorBackground::CheckpointDue() const
    {
        const auto& cache = volume.Cache();
        auto collection = volume.Collector().Due();
        auto due = volume.Journal().IsMoreThanHalfFull() || 2 * cache.ModifiedNodes() > cache.Capacity() || (collection && RoundLeft());
        auto nodeBlocks = (collection ? 2 : 1) * volume.Geometry().NodeBlocksPerCheckpoint();
        return due && volume.State().blocks.Count(NorBlockState::freeErased) > nodeBlocks;
    }

    bool NorBackground::RoundLeft() const
    {
        return CalledSinceRound() || volume.Collector().LeftBlocks() - roundStart < roundBlocks;
    }

    bool NorBackground::CalledSinceRound() const
    {
        return !roundReleases.has_value() || roundReleases.value_or(0) != volume.Queue().ForegroundReleases();
    }

    bool NorBackground::ShouldStop() const
    {
        return claimer.ShouldYield() || !HasWork();
    }

    void NorBackground::Claim()
    {
        claimer.Claim(machine.CompletionWith<void()>([]
            {
                return NorBackgroundGranted{};
            }));
    }

    void NorBackground::Work()
    {
        erasing = !CheckpointDue();
        if (erasing)
            volume.Eraser().Step(machine.CompletionWith<void()>([]
                {
                    return NorBackgroundUnitDone{};
                }));
        else
        {
            StartRound();
            volume.Checkpoints().Write(machine.CompletionWith<void(Result result)>([](Result result)
                {
                    // The pool holds every block that the checkpoint may take
                    really_assert(result == Result::success);
                    return NorBackgroundUnitDone{};
                }));
        }
    }

    void NorBackground::StartRound()
    {
        const auto& collector = volume.Collector();
        if (collector.Due() && CalledSinceRound())
        {
            roundReleases = volume.Queue().ForegroundReleases();
            roundStart = collector.LeftBlocks();
            roundBlocks = collector.CollectableBlocks();
        }
    }

    void NorBackground::Release()
    {
        if (erasing && claimer.ShouldYield())
            volume.Eraser().CountAsRefill();

        erasing = false;
        claimer.Release();
    }
}
