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
            volume.EntryUpdate().Commit(file, [this](Result result)
                {
                    Flush(result);
                });
        else
            Flush(Result::success);
    }

    void FatSync::Flush(Result result) const
    {
        if (result != Result::success)
            onDone(result);
        else
            volume.Cache().Flush(onDone);
    }
}
