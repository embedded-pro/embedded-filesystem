#include "fs/fat/FatRename.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    namespace
    {
        constexpr ShortNameBytes dotDot{ '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    }

    FatRename::FatRename(FatVolume& volume, FatRenameCall&& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , onFinished(onFinished)
        , parent(volume)
        , clear(volume.Cache(), volume.Reader(), volume.Geometry())
    {}

    FatRename::~FatRename() = default;

    void FatRename::Start()
    {
        if (IsRoot(call.from) || IsRoot(call.to))
        {
            Finish(Result::invalidPath);
            return;
        }

        parent.Find(call.from, false, [this](Result result)
            {
                OnSourceFound(result);
            });
    }

    void FatRename::OnSourceFound(Result result)
    {
        if (result == Result::success && !parent.Found().has_value())
            result = Result::notFound;

        if (result == Result::success)
        {
            source = *parent.Found();
            sourceParent = parent.Cluster();
            result = volume.Changeable(source);
        }

        if (result != Result::success)
            Finish(result);
        else
            parent.FindTarget(call.to, source, [this](Result result)
                {
                    OnTargetFound(result);
                });
    }

    void FatRename::OnTargetFound(Result result)
    {
        if (result == Result::success && parent.Found().has_value())
            result = Result::alreadyExists;

        if (result != Result::success || SameName())
            Finish(result);
        else
            parent.PrepareAlias([this](Result result)
                {
                    OnPrepared(result);
                });
    }

    bool FatRename::SameName() const
    {
        return parent.Cluster() == sourceParent && SplitLast(call.from).name == SplitLast(call.to).name;
    }

    void FatRename::OnPrepared(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else
            parent.WriteEntry(source.entry, [this](Result result)
                {
                    FlushThen(result, &FatRename::ClearSource);
                });
    }

    void FatRename::ClearSource()
    {
        volume.ClosedFiles().Forget(source.shortEntry);
        clear.Clear(source, [this](Result result)
            {
                if (result != Result::success || !Moved())
                    FlushThen(result, &FatRename::Succeed);
                else
                    UpdateParentEntry();
            });
    }

    void FatRename::UpdateParentEntry()
    {
        volume.Cache().Modify(volume.Geometry().SectorOfCluster(source.entry.firstCluster), entryRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success && !SetParent(contents))
                    result = Result::corrupted;

                FlushThen(result, &FatRename::Succeed);
            });
    }

    bool FatRename::Moved() const
    {
        return IsDirectory(source) && parent.Cluster() != sourceParent;
    }

    bool FatRename::SetParent(infra::ByteRange contents) const
    {
        auto raw = infra::Head(infra::DiscardHead(contents, directoryEntryBytes), directoryEntryBytes);
        auto entry = DecodeShortEntry(raw);

        if (entry.name.bytes != dotDot)
            return false;

        entry.firstCluster = parent.Cluster();
        EncodeShortEntry(entry, raw);
        return true;
    }

    void FatRename::Succeed()
    {
        Finish(Result::success);
    }

    void FatRename::FlushThen(Result result, Step next)
    {
        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        afterFlush = next;
        volume.Cache().Flush([this](Result result)
            {
                if (result != Result::success)
                    Finish(result);
                else
                    (this->*afterFlush)();
            });
    }

    void FatRename::Finish(Result result)
    {
        onFinished(result);
        call.onDone(result);
    }
}
