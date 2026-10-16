#include "fs/fat/FatTruncation.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatTruncation::FatTruncation(FatVolume& volume)
        : volume(volume)
        , table(volume.Cache(), volume.Geometry())
    {}

    FatTruncation::~FatTruncation() = default;

    void FatTruncation::Truncate(FatOpenFile& file, FatExtentCache& extents, const infra::Function<void(Result result)>& onDone)
    {
        auto clusterBytes = volume.Geometry().sectorsPerCluster * volume.Cache().SectorSize();
        this->file = &file;
        this->extents = &extents;
        this->onDone = onDone;
        kept = static_cast<uint32_t>((uint64_t{ file.position } + clusterBytes - 1) / clusterBytes);
        firstCut = std::nullopt;

        if (kept == file.clusters && file.position == file.size)
            this->onDone(Result::success);
        else if (kept == 0)
        {
            firstCut = file.firstCluster;
            Commit();
        }
        else if (kept == file.clusters)
            Commit();
        else
            LocateLastKept();
    }

    void FatTruncation::LocateLastKept()
    {
        volume.Locator().Locate(*extents, kept - 1, kept - 1, [this](Result result, const FatExtent& run)
            {
                if (result != Result::success)
                    onDone(result);
                else
                    ReadFirstCut(run.ClusterAt(kept - 1));
            });
    }

    void FatTruncation::ReadFirstCut(uint32_t cluster)
    {
        lastKept = cluster;
        table.Next(cluster, [this](Result result, std::optional<uint32_t> next)
            {
                if (result != Result::success)
                    onDone(result);
                else
                {
                    firstCut = next;
                    Commit();
                }
            });
    }

    void FatTruncation::Commit()
    {
        volume.MarkDirty([this](Result result)
            {
                if (result != Result::success)
                    onDone(result);
                else
                    CommitEntry();
            });
    }

    void FatTruncation::CommitEntry()
    {
        file->size = file->position;
        file->clusters = kept;
        file->written = true;
        extents->Truncate(kept);

        if (kept == 0)
            file->firstCluster = 0;

        volume.EntryUpdate().Commit(*file, [this](Result result)
            {
                FlushThen(result, &FatTruncation::CutChain);
            });
    }

    void FatTruncation::CutChain()
    {
        if (!firstCut.has_value())
            onDone(Result::success);
        else if (kept == 0)
            FreeCut();
        else
            volume.TableWriter().Write(lastKept, EndOfChain(volume.Geometry().type), [this](Result result)
                {
                    FlushThen(result, &FatTruncation::FreeCut);
                });
    }

    void FatTruncation::FreeCut()
    {
        volume.ChainFree().Free(*firstCut, [this](Result result)
            {
                FlushThen(result, &FatTruncation::Succeed);
            });
    }

    void FatTruncation::Succeed()
    {
        onDone(Result::success);
    }

    void FatTruncation::FlushThen(Result result, Step next)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        afterFlush = next;
        volume.Cache().Flush([this](Result result)
            {
                if (result != Result::success)
                    onDone(result);
                else
                    (this->*afterFlush)();
            });
    }
}
