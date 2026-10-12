#include "fs/fat/FatPathWalk.hpp"
#include "fs/fat/FatName.hpp"

namespace fs
{
    FatPathWalk::FatPathWalk(FatDirectoryReader& reader, const FatGeometry& geometry)
        : reader(reader)
        , geometry(geometry)
    {}

    FatPathWalk::~FatPathWalk() = default;

    void FatPathWalk::Find(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;
        components.emplace(path);
        found = std::nullopt;
        cursor = reader.Start(0);
        NextComponent();
    }

    bool FatPathWalk::FoundRoot() const
    {
        return !found.has_value();
    }

    const FatScannedEntry& FatPathWalk::Found() const
    {
        return *found;
    }

    bool FatPathWalk::FoundDirectory() const
    {
        return FoundRoot() || IsDirectory(*found);
    }

    uint32_t FatPathWalk::FoundCluster() const
    {
        return FoundRoot() ? 0 : found->entry.firstCluster;
    }

    bool FatPathWalk::Visit(const FatScannedEntry& entry)
    {
        if (!NameMatches(entry, component))
            return true;

        found = entry;
        return false;
    }

    void FatPathWalk::FreeSlot(FatEntryPosition, bool)
    {}

    void FatPathWalk::NextComponent()
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
        found = std::nullopt;
        reader.Read(cursor, *this, [this](Result result)
            {
                OnScanned(result);
            });
    }

    void FatPathWalk::OnScanned(Result result)
    {
        if (result != Result::success)
            onDone(result);
        else if (!found)
            onDone(Result::notFound);
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
}
