#include "fs/nor/NorUnmount.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    NorUnmount::NorUnmount(NorVolume& volume, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , onDone(onDone)
    {}

    NorUnmount::~NorUnmount() = default;

    void NorUnmount::Start() const
    {
        CommitNextFile();
    }

    void NorUnmount::CommitNextFile() const
    {
        for (auto& file : volume.OpenFiles())
            if (file.State().Writable() && file.State().Uncommitted())
            {
                volume.FileCommit().Commit(file.State(), [this](Result result)
                    {
                        if (result == Result::success)
                            CommitNextFile();
                        else
                            onDone(result);
                    });
                return;
            }

        Checkpoint();
    }

    void NorUnmount::Checkpoint() const
    {
        if (volume.Journal().IsMoreThanHalfFull())
            volume.Checkpoints().Write([this](Result result)
                {
                    onDone(result);
                });
        else
            onDone(Result::success);
    }
}
