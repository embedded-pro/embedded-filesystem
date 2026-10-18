#include "fs/fat/reader/FatReaderSearch.hpp"
#include "fs/fat/FatName.hpp"

namespace fs
{
    FatReaderSearch::FatReaderSearch(FatDirectoryReader& reader, const FatGeometry& geometry)
        : reader(reader)
        , geometry(geometry)
    {}

    FatReaderSearch::~FatReaderSearch() = default;

    void FatReaderSearch::Find(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;
        components.emplace(path);
        found = nullptr;
        lastMissing = false;
        cursor = reader.Start(0);
        NextComponent();
    }

    bool FatReaderSearch::FoundRoot() const
    {
        return found == nullptr;
    }

    const FatScannedEntry& FatReaderSearch::Found() const
    {
        return *found;
    }

    bool FatReaderSearch::FoundDirectory() const
    {
        return FoundRoot() || IsDirectory(*found);
    }

    uint32_t FatReaderSearch::FoundCluster() const
    {
        return FoundRoot() ? 0 : found->entry.firstCluster;
    }

    bool FatReaderSearch::LastMissing() const
    {
        return lastMissing;
    }

    infra::BoundedConstString FatReaderSearch::Component() const
    {
        return component;
    }

    const std::optional<FatEntryPosition>& FatReaderSearch::Free() const
    {
        return free;
    }

    bool FatReaderSearch::Extensible() const
    {
        return extensible;
    }

    bool FatReaderSearch::Visit(const FatScannedEntry& entry)
    {
        if (!NameMatches(entry, component))
            return true;

        found = &entry;
        return false;
    }

    void FatReaderSearch::FreeSlot(FatEntryPosition position, bool)
    {
        if (!free)
            free = position;
    }

    void FatReaderSearch::NextComponent()
    {
        auto next = components->Next();

        if (!next)
        {
            onDone(Result::success);
            return;
        }

        if (auto validation = ValidateFatName(*next); validation != Result::success)
        {
            onDone(validation);
            return;
        }

        component = *next;
        found = nullptr;
        free = std::nullopt;
        extensible = false;
        reader.Read(cursor, *this, [this](Result result)
            {
                Scanned(result);
            });
    }

    void FatReaderSearch::Scanned(Result result)
    {
        if (result != Result::success)
            onDone(result);
        else if (found == nullptr)
            Missed();
        else if (components->Done())
            onDone(Result::success);
        else if (!FoundDirectory())
            onDone(Result::notADirectory);
        else if (!geometry.IsCluster(FoundCluster()))
            onDone(Result::corrupted);
        else
        {
            cursor = reader.Start(FoundCluster());
            NextComponent();
        }
    }

    void FatReaderSearch::Missed()
    {
        lastMissing = components->Done();

        if (lastMissing && !free && cursor.cluster != 0)
        {
            free = FatEntryPosition{ cursor.cluster, reader.Capacity(cursor.cluster) };
            extensible = true;
        }

        onDone(Result::notFound);
    }
}
