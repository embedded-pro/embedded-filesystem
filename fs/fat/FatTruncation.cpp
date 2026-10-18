#include "fs/fat/FatTruncation.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatTruncation::FatTruncation(FatVolume& volume)
        : volume(volume)
        , table(volume.Cache(), volume.Geometry())
        , resume([this](Result result)
              {
                  Resume(result);
              })
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
        volume.MarkDirty(Then(Step::dirtied));
    }

    void FatTruncation::CommitEntry()
    {
        file->size = file->position;
        file->clusters = kept;
        file->written = true;
        extents->Truncate(kept);

        if (kept == 0)
            file->firstCluster = 0;

        volume.EntryUpdate().Commit(*file, Then(Step::entryCommitted));
    }

    void FatTruncation::CutChain()
    {
        if (!firstCut.has_value())
            onDone(Result::success);
        else if (kept == 0)
            FreeCut();
        else
            volume.TableWriter().Write(lastKept, EndOfChain(volume.Geometry().type), Then(Step::endWritten));
    }

    void FatTruncation::FreeCut()
    {
        volume.ChainFree().Free(*firstCut, Then(Step::chainFreed));
    }

    void FatTruncation::Flush(Step next)
    {
        volume.Cache().Flush(Then(next));
    }

    const infra::Function<void(Result result)>& FatTruncation::Then(Step next)
    {
        step = next;
        return resume;
    }

    void FatTruncation::Resume(Result result)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        switch (step)
        {
            case Step::dirtied:
                CommitEntry();
                break;
            case Step::entryCommitted:
                Flush(Step::cutChain);
                break;
            case Step::endWritten:
                Flush(Step::freeCut);
                break;
            case Step::chainFreed:
                Flush(Step::succeed);
                break;
            case Step::cutChain:
                CutChain();
                break;
            case Step::freeCut:
                FreeCut();
                break;
            default:
                onDone(Result::success);
                break;
        }
    }
}
