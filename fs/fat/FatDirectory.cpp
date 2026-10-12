#include "fs/fat/FatDirectory.hpp"

namespace fs
{
    FatDirectory::FatDirectory(FatVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    FatDirectory::~FatDirectory()
    {
        if (open)
            Detach();
    }

    void FatDirectory::Open(uint32_t firstCluster)
    {
        this->firstCluster = firstCluster;
        cursor = volume.Reader().Start(firstCluster);
        generation = volume.Generation();
        open = true;
        volume.OpenDirectories().push_back(*this);
    }

    uint32_t FatDirectory::FirstCluster() const
    {
        return firstCluster;
    }

    void FatDirectory::Detach()
    {
        volume.OpenDirectories().erase(*this);
        open = false;
    }

    void FatDirectory::Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone)
    {
        if (this->onDone)
            volume.Defer(onDone, Result::busy);
        else
        {
            this->onDone = onDone;
            self = Share(*this);
            claimer.Claim([this]
                {
                    Read();
                });
        }
    }

    void FatDirectory::UnReferenced()
    {
        if (open)
            Detach();

        Release();
    }

    bool FatDirectory::Visit(const FatScannedEntry& scanned)
    {
        entry = InfoOf(scanned);
        return false;
    }

    void FatDirectory::FreeSlot(FatEntryPosition, bool)
    {}

    void FatDirectory::Read()
    {
        if (!volume.Available(generation))
        {
            Done(volume.Unavailable());
            return;
        }

        entry = std::nullopt;
        volume.Reader().Read(cursor, *this, [this](Result result)
            {
                volume.Report(result);
                Done(result);
            });
    }

    void FatDirectory::Done(Result result)
    {
        claimer.Release();

        self = nullptr;
        onDone(result, result == Result::success ? entry : std::nullopt);
    }
}
