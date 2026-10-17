#include "fs/fat/fiber/FatFiberDirectory.hpp"
#include "infra/event/EventDispatcher.hpp"

namespace fs
{
    FatFiberDirectory::FatFiberDirectory(FatFiberVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatFiberDirectory::Open(uint32_t firstCluster)
    {
        generation = volume.Generation();
        cursor = volume.Start(firstCluster);
    }

    void FatFiberDirectory::Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone)
    {
        if (onNext)
            volume.Defer(onDone, Result::busy);
        else
        {
            onNext = onDone;
            self = Share(*this);
            claimer.Claim([this]
                {
                    volume.Fiber().Run(claimer, [this]
                        {
                            RunNext();
                        });
                });
        }
    }

    void FatFiberDirectory::UnReferenced()
    {
        Release();
    }

    void FatFiberDirectory::RunNext()
    {
        if (!volume.Available(generation))
            nextResult = volume.Unavailable();
        else
        {
            auto step = volume.NextEntry(cursor);
            nextResult = step.result;
            entry = step.found ? std::optional<EntryInfo>(InfoOf(volume.Entry())) : std::nullopt;
            volume.Report(nextResult);
        }

        infra::EventDispatcher::Instance().Schedule([this]
            {
                NextCompleted();
            });
    }

    void FatFiberDirectory::NextCompleted()
    {
        onNext(nextResult, nextResult == Result::success ? entry : std::nullopt);
        self = nullptr;
    }
}
