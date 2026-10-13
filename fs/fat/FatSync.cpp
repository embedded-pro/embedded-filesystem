#include "fs/fat/FatSync.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatSync::FatSync(FatVolume& volume, FatOpenFile& file, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , file(file)
        , onDone(onDone)
    {}

    FatSync::~FatSync() = default;

    void FatSync::Start() const
    {
        if (file.Uncommitted())
            volume.DirtyFlag().MarkDirty([this](Result result)
                {
                    if (result != Result::success)
                        onDone(result);
                    else
                        Commit();
                });
        else
            Flush(Result::success);
    }

    void FatSync::Commit() const
    {
        volume.EntryUpdate().Commit(file, [this](Result result)
            {
                Flush(result);
            });
    }

    void FatSync::Flush(Result result) const
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        volume.Cache().Flush([this](Result result)
            {
                onDone(result);
            });
    }
}
