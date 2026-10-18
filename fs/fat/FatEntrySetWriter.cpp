#include "fs/fat/FatEntrySetWriter.hpp"
#include "fs/fat/FatLongNames.hpp"
#include "fs/fat/FatRanks.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatEntrySetWriter::FatEntrySetWriter(SectorCache& cache, FatDirectoryReader& reader, FatAllocator& allocator, const FatGeometry& geometry, const FatLongNames* longNames)
        : cache(cache)
        , reader(reader)
        , allocator(allocator)
        , geometry(geometry)
        , longNames(longNames)
        , table(cache, geometry)
    {}

    FatEntrySetWriter::~FatEntrySetWriter() = default;

    void FatEntrySetWriter::Write(const FatEntrySet& set, const FatFreeSlots& slots, const infra::Function<void(Result result)>& onDone)
    {
        this->set = set;
        this->onDone = onDone;
        start = slots;
        capacity = reader.Capacity(start.first.cluster);
        this->slots = static_cast<uint8_t>(set.longName.empty() ? 1 : longNames->entriesFor(set.longName) + 1);
        checksum = LongNameChecksum(set.entry.name.bytes);
        clusters = { start.first.cluster, 0, 0 };
        resolved = 1;
        existing = 1;

        if (start.index + this->slots > FatDirectoryReader::maxEntries)
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    Done(Result::noSpace);
                });
        else
            Resolve();
    }

    FatScannedEntry FatEntrySetWriter::Written() const
    {
        FatScannedEntry written;
        written.entry = set.entry;
        written.name = set.longName;
        written.longName = !set.longName.empty();
        written.first = PositionOf(0);
        written.shortEntry = PositionOf(slots - 1u);
        written.slots = slots;
        return written;
    }

    void FatEntrySetWriter::Resolve()
    {
        if (ClusterIndexOf(slots - 1u) < resolved)
        {
            slot = 0;
            WriteSector();
        }
        else if (existing == resolved)
            table.Next(clusters[resolved - 1], [this](Result result, std::optional<uint32_t> next)
                {
                    OnNext(result, next);
                });
        else
            Grow();
    }

    void FatEntrySetWriter::OnNext(Result result, std::optional<uint32_t> next)
    {
        if (result != Result::success)
            Done(result);
        else if (next.has_value())
        {
            clusters[resolved++] = *next;
            ++existing;
            Resolve();
        }
        else
            Grow();
    }

    void FatEntrySetWriter::Grow()
    {
        allocator.Take(clusters[resolved - 1], 1, [this](Result result, uint32_t first, uint32_t)
            {
                OnTaken(result, first);
            });
    }

    void FatEntrySetWriter::OnTaken(Result result, uint32_t cluster)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        clusters[resolved++] = cluster;
        sector = geometry.SectorOfCluster(cluster);
        ZeroSector();
    }

    void FatEntrySetWriter::ZeroSector()
    {
        zeroing = true;
        UpdateSector();
    }

    void FatEntrySetWriter::OnZeroed(Result result)
    {
        if (result != Result::success)
            Done(result);
        else if (++sector != geometry.SectorOfCluster(clusters[resolved - 1]) + geometry.sectorsPerCluster)
            ZeroSector();
        else
            allocator.Link([this](Result result)
                {
                    OnLinked(result);
                });
    }

    void FatEntrySetWriter::OnLinked(Result result)
    {
        if (result != Result::success)
            Done(result);
        else
            Resolve();
    }

    void FatEntrySetWriter::WriteSector()
    {
        if (slot == slots || ClusterIndexOf(slot) >= existing)
        {
            Done(Result::success);
            return;
        }

        sector = SectorOfSlot(slot);
        zeroing = false;
        UpdateSector();
    }

    void FatEntrySetWriter::UpdateSector()
    {
        auto onSector = [this](Result result, infra::ByteRange contents)
        {
            if (result == Result::success)
                Fill(contents);

            if (zeroing)
                OnZeroed(result);
            else
                OnWritten(result);
        };

        if (zeroing)
            cache.Overwrite(sector, dataRank, onSector);
        else
            cache.Modify(sector, RankOf(sector), onSector);
    }

    void FatEntrySetWriter::OnWritten(Result result)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        while (slot != slots && SectorOfSlot(slot) == sector)
            ++slot;

        WriteSector();
    }

    void FatEntrySetWriter::Fill(infra::ByteRange contents) const
    {
        auto perSector = cache.SectorSize() / directoryEntryBytes;

        for (uint32_t index = 0; index != slots && ClusterIndexOf(index) < resolved; ++index)
        {
            if (SectorOfSlot(index) != sector)
                continue;

            auto out = infra::Head(infra::DiscardHead(contents, PositionOf(index).index % perSector * directoryEntryBytes), directoryEntryBytes);

            if (index + 1u == slots)
                EncodeShortEntry(set.entry, out);
            else
                longNames->encode(set.longName, static_cast<uint8_t>(slots - 1u - index), index == 0, checksum, out);
        }
    }

    void FatEntrySetWriter::Done(Result result)
    {
        onDone(result);
    }

    uint32_t FatEntrySetWriter::ClusterIndexOf(uint32_t slot) const
    {
        return (start.first.index + slot) / capacity;
    }

    FatEntryPosition FatEntrySetWriter::PositionOf(uint32_t slot) const
    {
        auto offset = start.first.index + slot;
        really_assert(offset / capacity < clusters.size());

        return FatEntryPosition{ clusters[offset / capacity], offset % capacity };
    }

    uint32_t FatEntrySetWriter::SectorOfSlot(uint32_t slot) const
    {
        return reader.SectorOf(PositionOf(slot));
    }

    uint8_t FatEntrySetWriter::RankOf(uint32_t sector) const
    {
        auto shortEntrySector = SectorOfSlot(slots - 1u);

        if (sector == shortEntrySector && SectorOfSlot(0) != shortEntrySector)
            return trailingEntryRank;

        return entryRank;
    }
}
