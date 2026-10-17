#include "fs/fat/coro/FatCoroDirectory.hpp"
#include "infra/event/EventDispatcher.hpp"

namespace fs
{
    FatCoroDirectory::FatCoroDirectory(FatCoroVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatCoroDirectory::Open(uint32_t firstCluster)
    {
        generation = volume.Generation();
        cursor = volume.Start(firstCluster);
    }

    void FatCoroDirectory::Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone)
    {
        if (onNext)
            volume.Defer(onDone, Result::busy);
        else
        {
            onNext = onDone;
            self = Share(*this);
            RunNext(frames);
        }
    }

    std::size_t FatCoroDirectory::FramePeak() const
    {
        return frames.Peak();
    }

    void FatCoroDirectory::UnReferenced()
    {
        Release();
    }

    coro::Detached FatCoroDirectory::RunNext(coro::Frames&)
    {
        co_await coro::Claim{ claimer };

        if (!volume.Available(generation))
            nextResult = volume.Unavailable();
        else
        {
            auto step = co_await volume.NextEntry(frames, cursor);
            nextResult = step.result;
            entry = step.found ? std::optional<EntryInfo>(InfoOf(volume.Entry())) : std::nullopt;
            volume.Report(nextResult);
        }

        claimer.Release();
        infra::EventDispatcher::Instance().Schedule([this]
            {
                NextCompleted();
            });
    }

    void FatCoroDirectory::NextCompleted()
    {
        onNext(nextResult, nextResult == Result::success ? entry : std::nullopt);
        self = nullptr;
    }
}
