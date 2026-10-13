#include "fs/fat/FatUnmount.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatUnmount::FatUnmount(FatVolume& volume, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , onDone(onDone)
    {}

    FatUnmount::~FatUnmount() = default;

    void FatUnmount::Start() const
    {
        CommitNextFile();
    }

    void FatUnmount::CommitNextFile() const
    {
        for (auto& file : volume.OpenFiles())
            if (file.State().Uncommitted())
            {
                volume.EntryUpdate().Commit(file.State(), [this](Result result)
                    {
                        if (result != Result::success)
                            Done(result);
                        else
                            CommitNextFile();
                    });
                return;
            }

        WriteFsInfo();
    }

    void FatUnmount::WriteFsInfo() const
    {
        const auto& state = volume.State();

        if (!state.geometry.fsInfoSector.has_value() || !state.tableChanged)
        {
            Flush(Result::success);
            return;
        }

        volume.Cache().Modify(*state.geometry.fsInfoSector, tableRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success)
                    SerializeFsInfo(FsInfo{ volume.State().freeClusters, volume.Allocator().Cursor() }, contents);

                Flush(result);
            });
    }

    void FatUnmount::Flush(Result previous) const
    {
        volume.Cache().Flush([this, previous](Result result)
            {
                Done(previous != Result::success ? previous : result);
            });
    }

    void FatUnmount::Done(Result result) const
    {
        volume.Cache().Clear();
        volume.Cache().SetMirroredRegion(std::nullopt);
        onDone(result);
    }
}
