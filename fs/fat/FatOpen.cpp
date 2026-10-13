#include "fs/fat/FatOpen.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatVolume.hpp"
#include <utility>

namespace fs
{
    FatOpen::FatOpen(FatVolume& volume, FatOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , allocate(allocate)
        , onFinished(onFinished)
        , parent(volume)
    {}

    FatOpen::~FatOpen() = default;

    void FatOpen::Start()
    {
        if (IsRoot(call.path))
        {
            Finish(Result::isADirectory);
            return;
        }

        parent.Find(call.path, call.mode != OpenMode::read, [this](Result result)
            {
                OnFound(result);
            });
    }

    void FatOpen::OnFound(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else if (parent.Found())
            OpenExisting(*parent.Found());
        else if (call.mode == OpenMode::read)
            Finish(Result::notFound);
        else
            Create();
    }

    void FatOpen::OpenExisting(const FatScannedEntry& found)
    {
        auto result = IsDirectory(found) ? Result::isADirectory : VerifyFile(found);

        if (result == Result::success)
            file = allocate();

        if (file != nullptr)
            file->Open(found, call.mode);
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        Finish(result);
    }

    Result FatOpen::VerifyFile(const FatScannedEntry& found) const
    {
        if (call.mode == OpenMode::write)
            return Result::unsupported;

        if (call.mode != OpenMode::read && (found.entry.attributes & FatAttributes::readOnly) != 0)
            return Result::readOnly;

        if (found.entry.size != 0 && !volume.Geometry().IsCluster(found.entry.firstCluster))
            return Result::corrupted;

        for (auto& open : volume.OpenFiles())
            if (open.State().entry == found.shortEntry && (open.State().mode != OpenMode::read || call.mode != OpenMode::read))
                return Result::busy;

        return Result::success;
    }

    void FatOpen::Create()
    {
        file = allocate();

        if (file == nullptr)
            Finish(Result::tooManyOpenFiles);
        else
            parent.PrepareAlias([this](Result result)
                {
                    OnPrepared(result);
                });
    }

    void FatOpen::OnPrepared(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else
            parent.Write(FatAttributes::archive, 0, [this](Result result)
                {
                    OnCreated(result);
                });
    }

    void FatOpen::OnCreated(Result result)
    {
        if (result == Result::success)
            file->Open(parent.Written(), call.mode);

        Finish(result);
    }

    void FatOpen::Finish(Result result)
    {
        auto opened = std::exchange(file, nullptr);
        onFinished(result);
        call.onDone(result, result == Result::success ? opened : nullptr);
    }
}
