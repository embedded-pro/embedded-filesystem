#include "fs/fat/FatOpen.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatOpen::FatOpen(FatVolume& volume, FatOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , allocate(allocate)
        , onFinished(onFinished)
    {}

    FatOpen::~FatOpen() = default;

    void FatOpen::Start()
    {
        volume.Walk().Find(call.path, [this](Result result)
            {
                OnFound(result);
            });
    }

    void FatOpen::OnFound(Result result)
    {
        onFinished(result);
        result = Verify(result);

        infra::SharedPtr<FatFile> file;

        if (result == Result::success)
            file = allocate();

        if (file != nullptr)
            file->Open(volume.Walk().Found(), call.mode);
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        call.onDone(result, file);
    }

    Result FatOpen::Verify(Result result) const
    {
        const auto& walk = volume.Walk();

        if (result == Result::notFound && call.mode != OpenMode::read)
            return Result::unsupported;

        if (result != Result::success)
            return result;

        if (walk.FoundDirectory())
            return Result::isADirectory;

        return VerifyFile(walk.Found());
    }

    Result FatOpen::VerifyFile(const FatScannedEntry& found) const
    {
        if (call.mode == OpenMode::write)
            return Result::unsupported;

        if (call.mode != OpenMode::read && (found.entry.attributes & FatAttributes::readOnly) != 0)
            return Result::readOnly;

        if (found.entry.size != 0 && !volume.Geometry().IsCluster(found.entry.firstCluster))
            return Result::corrupted;

        for (auto& file : volume.OpenFiles())
            if (file.State().entry == found.shortEntry && (file.State().mode != OpenMode::read || call.mode != OpenMode::read))
                return Result::busy;

        return Result::success;
    }
}
