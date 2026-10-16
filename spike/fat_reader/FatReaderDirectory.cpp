#include "spike/fat_reader/FatReaderDirectory.hpp"

namespace spike
{
    FatReaderDirectory::FatReaderDirectory(FatReaderVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatReaderDirectory::Open(uint32_t cluster)
    {
        generation = volume.Generation();
        position = FatPosition{ cluster, 0, 0 };
    }

    void FatReaderDirectory::Next(const infra::Function<void(fs::Result result, std::optional<fs::EntryInfo> entry)>& onDone)
    {
        if (this->onDone)
            volume.Defer(onDone, fs::Result::busy);
        else
        {
            this->onDone = onDone;
            self = Share(*this);
            claimer.Claim([this]
                {
                    Granted();
                });
        }
    }

    void FatReaderDirectory::UnReferenced()
    {
        Release();
    }

    void FatReaderDirectory::Granted()
    {
        if (volume.Available(generation))
            volume.Scan().Next(position, [this](fs::Result result, std::optional<FatEntry> entry)
                {
                    Scanned(result, entry);
                });
        else
            Scanned(fs::Result::notMounted, std::nullopt);
    }

    void FatReaderDirectory::Scanned(fs::Result result, std::optional<FatEntry> entry)
    {
        claimer.Release();

        if (entry.has_value())
            onDone(result, fs::EntryInfo{ entry->name, entry->directory ? fs::EntryType::directory : fs::EntryType::file, entry->directory ? 0 : entry->size });
        else
            onDone(result, std::nullopt);

        self = nullptr;
    }
}
