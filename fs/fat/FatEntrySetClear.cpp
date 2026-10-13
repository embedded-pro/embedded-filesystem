#include "fs/fat/FatEntrySetClear.hpp"
#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatRanks.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        constexpr uint8_t freeEntry = 0xe5;
    }

    FatEntrySetClear::FatEntrySetClear(SectorCache& cache, FatDirectoryReader& reader, const FatGeometry& geometry)
        : cache(cache)
        , reader(reader)
        , table(cache, geometry)
    {}

    FatEntrySetClear::~FatEntrySetClear() = default;

    void FatEntrySetClear::Clear(const FatScannedEntry& entry, const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;
        first = entry.first;
        shortEntry = entry.shortEntry;
        slots = entry.slots;
        slot = 0;
        capacity = reader.Capacity(first.cluster);
        clusters = { first.cluster, shortEntry.cluster, shortEntry.cluster };

        auto inFirst = capacity - first.index;

        if (first.cluster != shortEntry.cluster && slots > inFirst + shortEntry.index + 1)
            table.Next(first.cluster, [this](Result result, std::optional<uint32_t> next)
                {
                    OnMiddle(result, next);
                });
        else
            ClearSector(reader.SectorOf(shortEntry), entryRank);
    }

    void FatEntrySetClear::OnMiddle(Result result, std::optional<uint32_t> next)
    {
        if (result != Result::success || !next.has_value())
        {
            onDone(result != Result::success ? result : Result::corrupted);
            return;
        }

        clusters[1] = *next;
        ClearSector(reader.SectorOf(shortEntry), entryRank);
    }

    void FatEntrySetClear::ClearSector(uint32_t sector, uint8_t rank)
    {
        this->sector = sector;
        cache.Modify(sector, rank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success)
                    Mark(contents);

                OnCleared(result);
            });
    }

    void FatEntrySetClear::OnCleared(Result result)
    {
        if (result != Result::success)
            onDone(result);
        else
            ClearNextSector();
    }

    void FatEntrySetClear::ClearNextSector()
    {
        auto shortSector = reader.SectorOf(shortEntry);

        while (slot != slots && (SectorOfSlot(slot) == shortSector || SectorOfSlot(slot) == sector))
            ++slot;

        if (slot == slots)
            onDone(Result::success);
        else
            ClearSector(SectorOfSlot(slot), trailingEntryRank);
    }

    void FatEntrySetClear::Mark(infra::ByteRange contents) const
    {
        auto perSector = cache.SectorSize() / directoryEntryBytes;

        for (uint32_t index = 0; index != slots; ++index)
            if (SectorOfSlot(index) == sector)
                contents[PositionOf(index).index % perSector * directoryEntryBytes] = freeEntry;
    }

    FatEntryPosition FatEntrySetClear::PositionOf(uint32_t slot) const
    {
        auto offset = first.index + slot;
        really_assert(offset / capacity < clusters.size());

        return FatEntryPosition{ clusters[offset / capacity], offset % capacity };
    }

    uint32_t FatEntrySetClear::SectorOfSlot(uint32_t slot) const
    {
        return reader.SectorOf(PositionOf(slot));
    }
}
