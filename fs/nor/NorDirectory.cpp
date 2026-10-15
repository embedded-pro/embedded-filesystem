#include "fs/nor/NorDirectory.hpp"
#include "fs/nor/NorVolume.hpp"
#include <limits>

namespace fs
{
    NorDirectory::NorDirectory(NorVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    NorDirectory::~NorDirectory() = default;

    void NorDirectory::Open(uint32_t directory)
    {
        inode = directory;
        nextKey = 0;
        ended = false;
        generation = volume.Generation();
        open = true;
        volume.OpenDirectories().push_back(*this);
    }

    uint32_t NorDirectory::Inode() const
    {
        return inode;
    }

    void NorDirectory::Detach()
    {
        volume.OpenDirectories().erase(*this);
        open = false;
    }

    void NorDirectory::Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onNext)
    {
        if (onDone)
            volume.Defer(onNext, Result::busy);
        else
        {
            onDone = onNext;
            self = Share(*this);
            claimer.Claim([this]
                {
                    Read();
                });
        }
    }

    void NorDirectory::UnReferenced()
    {
        if (open)
            Detach();

        Release();
    }

    void NorDirectory::Read()
    {
        if (!volume.Available(generation))
            Done(Result::notMounted);
        else if (ended)
            Done(Result::success);
        else
            volume.DirectoryRead().Read(volume.State().inodeTable, inode, nextKey, [this](Result result)
                {
                    Done(result);
                });
    }

    void NorDirectory::Done(Result result)
    {
        claimer.Release();
        self = nullptr;

        const auto& found = volume.DirectoryRead().Found();
        if (result != Result::success || ended || !found.has_value())
        {
            onDone(result, std::nullopt);
            return;
        }

        ended = found->key == std::numeric_limits<uint32_t>::max();
        nextKey = found->key + 1;
        onDone(result, volume.DirectoryRead().Info());
    }
}
