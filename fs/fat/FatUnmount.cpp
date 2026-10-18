#include "fs/fat/FatUnmount.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatUnmount::FatUnmount(FatVolume& volume, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , onDone(onDone)
        , resume([this](Result result)
              {
                  Resume(result);
              })
    {}

    FatUnmount::~FatUnmount() = default;

    void FatUnmount::Start()
    {
        CommitNextFile();
    }

    void FatUnmount::CommitNextFile()
    {
        for (auto& file : volume.OpenFiles())
            if (file.State().Uncommitted())
            {
                volume.EntryUpdate().Commit(file.State(), Then(Step::fileCommitted));
                return;
            }

        WriteFsInfo();
    }

    void FatUnmount::FileCommitted(Result result)
    {
        if (result != Result::success)
            Done(result);
        else
            CommitNextFile();
    }

    void FatUnmount::WriteFsInfo()
    {
        const auto& state = volume.State();

        if (!state.geometry.fsInfoSector.has_value() || !state.tableChanged)
        {
            MarkClean(Result::success);
            return;
        }

        volume.Cache().Modify(*state.geometry.fsInfoSector, tableRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success)
                    SerializeFsInfo(FsInfo{ volume.State().freeClusters, volume.Allocator().Cursor() }, contents);

                MarkClean(result);
            });
    }

    void FatUnmount::MarkClean(Result previous)
    {
        if (previous != Result::success)
            Flush(previous);
        else
            volume.MarkClean(Then(Step::markedClean));
    }

    void FatUnmount::Flush(Result previous)
    {
        beforeFlush = previous;
        volume.Cache().Flush(Then(Step::flushed));
    }

    void FatUnmount::Done(Result result) const
    {
        volume.Cache().Clear();
        volume.Cache().SetMirroredRegion(std::nullopt);
        onDone(result);
    }

    const infra::Function<void(Result result)>& FatUnmount::Then(Step next)
    {
        step = next;
        return resume;
    }

    void FatUnmount::Resume(Result result)
    {
        switch (step)
        {
            case Step::fileCommitted:
                FileCommitted(result);
                break;
            case Step::markedClean:
                Flush(result);
                break;
            default:
                Done(beforeFlush != Result::success ? beforeFlush : result);
                break;
        }
    }
}
