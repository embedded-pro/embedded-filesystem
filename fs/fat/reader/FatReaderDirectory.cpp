#include "fs/fat/reader/FatReaderDirectory.hpp"

namespace fs
{
    FatReaderDirectory::FatReaderDirectory(FatReaderVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatReaderDirectory::Open(uint32_t firstCluster)
    {
        generation = volume.Generation();
        cursor = volume.Reader().Start(firstCluster);
    }

    void FatReaderDirectory::Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone)
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

    void FatReaderDirectory::UnReferenced()
    {
        Release();
    }

    bool FatReaderDirectory::Visit(const FatScannedEntry& scanned)
    {
        entry = InfoOf(scanned);
        return false;
    }

    void FatReaderDirectory::FreeSlot(FatEntryPosition, bool)
    {}

    void FatReaderDirectory::Read()
    {
        if (!volume.Available(generation))
        {
            Done(volume.Unavailable());
            return;
        }

        entry = std::nullopt;
        volume.Reader().Read(cursor, *this, [this](Result result)
            {
                Done(result);
            });
    }

    void FatReaderDirectory::Done(Result result)
    {
        volume.Report(result);
        claimer.Release();
        onDone(result, result == Result::success ? entry : std::nullopt);
        self = nullptr;
    }
}
