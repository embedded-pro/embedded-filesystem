#include "fs/fat/FatChainFree.hpp"

namespace fs
{
    FatChainFree::FatChainFree(SectorCache& cache, DirectTransfers& transfers, FatVolumeState& volume, FatTableWriter& writer, FatFreeSpaceScan& freeSpace, bool discard)
        : transfers(transfers)
        , volume(volume)
        , writer(writer)
        , freeSpace(freeSpace)
        , table(cache, volume.geometry)
        , discard(discard)
    {}

    FatChainFree::~FatChainFree() = default;

    void FatChainFree::Free(uint32_t first, const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;
        current = first;
        run = FatReservation{ first, 0 };
        ReadNext();
    }

    void FatChainFree::ReadNext()
    {
        table.Next(current, [this](Result result, std::optional<uint32_t> next)
            {
                OnNext(result, next);
            });
    }

    void FatChainFree::OnNext(Result result, std::optional<uint32_t> next)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        this->next = next;
        Update(Step::freed);
    }

    void FatChainFree::OnFreed(Result result)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        ++run.count;
        volume.tableChanged = true;
        freeSpace.Freed(current);

        if (next == current + 1)
        {
            current = *next;
            ReadNext();
        }
        else
            DiscardRun();
    }

    void FatChainFree::DiscardRun()
    {
        if (!discard)
        {
            OnDiscarded(Result::success);
            return;
        }

        Update(Step::discarded);
    }

    void FatChainFree::Update(Step next)
    {
        auto onUpdated = [this](Result result)
        {
            Updated(result);
        };

        step = next;

        if (next == Step::freed)
            writer.Write(current, 0, onUpdated);
        else
        {
            auto begin = volume.geometry.SectorOfCluster(run.first);
            transfers.Erase(begin, begin + run.count * volume.geometry.sectorsPerCluster, onUpdated);
        }
    }

    void FatChainFree::Updated(Result result)
    {
        if (step == Step::freed)
            OnFreed(result);
        else
            OnDiscarded(result);
    }

    void FatChainFree::OnDiscarded(Result result)
    {
        if (result != Result::success || !next.has_value())
            onDone(result);
        else
        {
            current = *next;
            run = FatReservation{ current, 0 };
            ReadNext();
        }
    }
}
