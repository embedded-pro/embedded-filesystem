#include "fs/fat/FatOpenDirectory.hpp"
#include "fs/fat/FatDirectory.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatOpenDirectory::FatOpenDirectory(FatVolume& volume, const FatOpenDirectoryCall& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(call)
        , allocate(allocate)
        , onFinished(onFinished)
    {}

    FatOpenDirectory::~FatOpenDirectory() = default;

    void FatOpenDirectory::Start()
    {
        volume.Walk().Find(call.path, [this](Result result)
            {
                OnFound(result);
            });
    }

    void FatOpenDirectory::OnFound(Result result)
    {
        onFinished(result);
        result = Verify(result);

        infra::SharedPtr<FatDirectory> directory;

        if (result == Result::success)
            directory = allocate();

        if (directory != nullptr)
            directory->Open(volume.Walk().FoundCluster());
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        call.onDone(result, directory);
    }

    Result FatOpenDirectory::Verify(Result result) const
    {
        const auto& walk = volume.Walk();

        if (result != Result::success)
            return result;

        if (!walk.FoundDirectory())
            return Result::notADirectory;

        if (!walk.FoundRoot() && !volume.Geometry().IsCluster(walk.FoundCluster()))
            return Result::corrupted;

        return Result::success;
    }
}
