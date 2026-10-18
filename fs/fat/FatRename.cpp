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

        parent.Find(call.from, false, Then(Step::sourceFound));
    }

    void FatRename::OnSourceFound()
    {
        if (!parent.Found().has_value())
        {
            Finish(Result::notFound);
            return;
        }

        source = *parent.Found();
        sourceParent = parent.Cluster();

        if (auto result = volume.Changeable(source); result != Result::success)
            Finish(result);
        else
            parent.FindTarget(call.to, source, Then(Step::targetFound));
    }

    void FatRename::OnTargetFound()
    {
        if (parent.Found().has_value())
            Finish(Result::alreadyExists);
        else if (SameName())
            Finish(Result::success);
        else
            parent.PrepareAlias(Then(Step::prepared));
    }

    bool FatRename::SameName() const
    {
        return parent.Cluster() == sourceParent && SplitLast(call.from).name == SplitLast(call.to).name;
    }

    void FatRename::ClearSource()
    {
        volume.ClosedFiles().Forget(source.shortEntry);
        clear.Clear(source, Then(Step::sourceCleared));
    }

    void FatRename::OnSourceCleared()
    {
        if (Moved())
            UpdateParentEntry();
        else
            Flush(Step::succeed);
    }

    void FatRename::UpdateParentEntry()
    {
        volume.Cache().Modify(volume.Geometry().SectorOfCluster(source.entry.firstCluster), entryRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success && !SetParent(contents))
                    result = Result::corrupted;

                if (result != Result::success)
                    Finish(result);
                else
                    Flush(Step::succeed);
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

    void FatRename::Flush(Step next)
    {
        volume.Cache().Flush(Then(next));
    }

    void FatRename::Finish(Result result)
    {
        onFinished(result);
        call.onDone(result);
    }

    infra::Function<void(Result result)> FatRename::Then(Step next)
    {
        step = next;
        return [this](Result result)
        {
            Resume(result);
        };
    }

    void FatRename::Resume(Result result)
    {
        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        switch (step)
        {
            case Step::sourceFound:
                OnSourceFound();
                break;
            case Step::targetFound:
                OnTargetFound();
                break;
            case Step::prepared:
                parent.WriteEntry(source.entry, Then(Step::entryWritten));
                break;
            case Step::entryWritten:
                Flush(Step::clearSource);
                break;
            case Step::clearSource:
                ClearSource();
                break;
            case Step::sourceCleared:
                OnSourceCleared();
                break;
            default:
                Finish(Result::success);
                break;
        }
    }
}
