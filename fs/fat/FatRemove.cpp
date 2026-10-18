#include "fs/fat/FatRemove.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatRemove::FatRemove(FatVolume& volume, FatRemoveCall&& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , onFinished(onFinished)
        , parent(volume)
        , clear(volume.Cache(), volume.Reader(), volume.Geometry())
    {}

    FatRemove::~FatRemove() = default;

    void FatRemove::Start()
    {
        if (IsRoot(call.path))
        {
            Finish(Result::invalidPath);
            return;
        }

        parent.Find(call.path, false, Then(Step::found));
    }

    bool FatRemove::Visit(const FatScannedEntry&)
    {
        empty = false;
        return false;
    }

    void FatRemove::FreeSlot(FatEntryPosition, bool)
    {}

    void FatRemove::OnFound()
    {
        if (!parent.Found())
        {
            Finish(Result::notFound);
            return;
        }

        target = *parent.Found();

        if (auto result = Check(); result != Result::success)
            Finish(result);
        else if (IsDirectory(target))
            CheckEmpty();
        else
            ClearEntry();
    }

    Result FatRemove::Check() const
    {
        if ((target.entry.attributes & FatAttributes::readOnly) != 0)
            return Result::readOnly;

        return volume.Changeable(target);
    }

    void FatRemove::CheckEmpty()
    {
        cursor = volume.Reader().Start(target.entry.firstCluster);
        volume.Reader().Read(cursor, *this, Then(Step::checked));
    }

    void FatRemove::OnChecked()
    {
        if (!empty)
            Finish(Result::notEmpty);
        else
            ClearEntry();
    }

    void FatRemove::ClearEntry()
    {
        volume.MarkDirty(Then(Step::dirtied));
    }

    void FatRemove::ClearMarkedEntry()
    {
        volume.ClosedFiles().Forget(target.shortEntry);
        clear.Clear(target, Then(Step::cleared));
    }

    void FatRemove::FreeChain()
    {
        if (target.entry.firstCluster == 0)
            Finish(Result::success);
        else
            volume.ChainFree().Free(target.entry.firstCluster, Then(Step::freed));
    }

    void FatRemove::Finish(Result result)
    {
        onFinished(result);
        call.onDone(result);
    }

    infra::Function<void(Result result)> FatRemove::Then(Step next)
    {
        step = next;
        return [this](Result result)
        {
            Resume(result);
        };
    }

    void FatRemove::Resume(Result result)
    {
        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        switch (step)
        {
            case Step::found:
                OnFound();
                break;
            case Step::checked:
                OnChecked();
                break;
            case Step::dirtied:
                ClearMarkedEntry();
                break;
            case Step::cleared:
                volume.Cache().Flush(Then(Step::flushed));
                break;
            case Step::flushed:
                FreeChain();
                break;
            case Step::freed:
                volume.Cache().Flush(Then(Step::finish));
                break;
            default:
                Finish(Result::success);
                break;
        }
    }
}
