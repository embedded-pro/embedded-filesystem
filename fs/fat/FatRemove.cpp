#include "fs/fat/FatRemove.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatDirectory.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatVolume.hpp"
#include <algorithm>

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

        parent.Find(call.path, false, [this](Result result)
            {
                OnFound(result);
            });
    }

    bool FatRemove::Visit(const FatScannedEntry&)
    {
        empty = false;
        return false;
    }

    void FatRemove::FreeSlot(FatEntryPosition, bool)
    {}

    void FatRemove::OnFound(Result result)
    {
        if (result == Result::success && !parent.Found())
            result = Result::notFound;

        if (result == Result::success)
        {
            target = *parent.Found();
            result = Check();
        }

        if (result != Result::success)
            Finish(result);
        else if (IsDirectory(target))
            CheckEmpty();
        else
            ClearEntry();
    }

    Result FatRemove::Check() const
    {
        const auto& entry = target.entry;

        if ((entry.attributes & FatAttributes::readOnly) != 0)
            return Result::readOnly;

        if ((entry.firstCluster != 0 || IsDirectory(target)) && !volume.Geometry().IsCluster(entry.firstCluster))
            return Result::corrupted;

        return InUse() ? Result::busy : Result::success;
    }

    bool FatRemove::InUse() const
    {
        if (IsDirectory(target))
            return std::ranges::any_of(volume.OpenDirectories(), [this](const FatDirectory& directory)
                {
                    return directory.FirstCluster() == target.entry.firstCluster;
                });

        return std::ranges::any_of(volume.OpenFiles(), [this](const FatFile& file)
            {
                return file.State().entry == target.shortEntry;
            });
    }

    void FatRemove::CheckEmpty()
    {
        cursor = volume.Reader().Start(target.entry.firstCluster);
        volume.Reader().Read(cursor, *this, [this](Result result)
            {
                OnChecked(result);
            });
    }

    void FatRemove::OnChecked(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else if (!empty)
            Finish(Result::notEmpty);
        else
            ClearEntry();
    }

    void FatRemove::ClearEntry()
    {
        volume.ClosedFiles().Forget(target.shortEntry);
        clear.Clear(target, [this](Result result)
            {
                OnCleared(result);
            });
    }

    void FatRemove::OnCleared(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else
            volume.Cache().Flush([this](Result result)
                {
                    OnFlushed(result);
                });
    }

    void FatRemove::OnFlushed(Result result)
    {
        if (result != Result::success || target.entry.firstCluster == 0)
            Finish(result);
        else
            volume.ChainFree().Free(target.entry.firstCluster, [this](Result result)
                {
                    OnFreed(result);
                });
    }

    void FatRemove::OnFreed(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else
            volume.Cache().Flush([this](Result result)
                {
                    Finish(result);
                });
    }

    void FatRemove::Finish(Result result)
    {
        onFinished(result);
        call.onDone(result);
    }
}
