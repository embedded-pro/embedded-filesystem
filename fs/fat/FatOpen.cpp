#include "fs/fat/FatOpen.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatOpen::FatOpen(FatVolume& volume, const FatOpenCall& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(call)
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

        if (call.mode != OpenMode::read)
            return Result::unsupported;

        if (walk.Found().entry.size != 0 && !volume.Geometry().IsCluster(walk.Found().entry.firstCluster))
            return Result::corrupted;

        return Result::success;
    }
}
