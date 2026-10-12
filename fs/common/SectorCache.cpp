#include "fs/common/SectorCache.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <tuple>
#include <utility>

namespace fs
{
    namespace
    {
        using Entry = SectorCache::Entry;
        using MirroredRegion = SectorCache::MirroredRegion;

        uint32_t SectorSizeOf(infra::MemoryRange<Entry> entries, infra::ByteRange buffers)
        {
            really_assert(!entries.empty() && buffers.size() % entries.size() == 0 && buffers.size() >= entries.size());

            return static_cast<uint32_t>(buffers.size() / entries.size());
        }

        Entry* Find(infra::MemoryRange<Entry> entries, uint32_t sector)
        {
            for (auto& entry : entries)
                if (entry.valid && entry.sector == sector)
                    return &entry;

            return nullptr;
        }

        std::pair<uint8_t, uint8_t> EvictionOrder(const Entry& entry)
        {
            if (!entry.valid)
                return { 0, 0 };

            if (!entry.dirty)
                return { 1, 0 };

            return { 2, entry.rank };
        }

        Entry& SelectSlot(infra::IntrusiveList<Entry>& leastRecentlyUsed)
        {
            return *std::min_element(leastRecentlyUsed.begin(), leastRecentlyUsed.end(), [](const Entry& x, const Entry& y)
                {
                    return EvictionOrder(x) < EvictionOrder(y);
                });
        }

        Entry* NextModified(infra::MemoryRange<Entry> entries)
        {
            Entry* next = nullptr;

            for (auto& entry : entries)
                if (entry.dirty && (next == nullptr || std::tie(entry.rank, entry.sector) < std::tie(next->rank, next->sector)))
                    next = &entry;

            return next;
        }

        uint8_t Copies(const std::optional<MirroredRegion>& region, uint32_t sector)
        {
            if (region && sector >= region->firstSector && sector - region->firstSector < region->sectors)
                return region->copies;

            return 1;
        }

        uint32_t CopySector(const std::optional<MirroredRegion>& region, uint32_t sector, uint8_t copyIndex)
        {
            if (copyIndex == 0 || !region)
                return sector;

            return sector + copyIndex * region->sectors;
        }
    }

    SectorCache::SectorCache(FlushBarrier& barrier, infra::MemoryRange<Entry> entries, infra::ByteRange buffers)
        : barrier(barrier)
        , entries(entries)
        , buffers(buffers)
        , sectorSize(SectorSizeOf(entries, buffers))
    {
        for (auto& entry : entries)
            leastRecentlyUsed.push_back(entry);
    }

    uint32_t SectorCache::SectorSize() const
    {
        return sectorSize;
    }

    void SectorCache::SetMirroredRegion(std::optional<MirroredRegion> region)
    {
        really_assert(!region || region->copies != 0);

        mirroredRegion = region;
    }

    void SectorCache::Read(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone)
    {
        Start(Request::read, sector, 0);
        onRead = onDone;
        Access();
    }

    void SectorCache::ReadOnce(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone)
    {
        Start(Request::readOnce, sector, 0);
        onRead = onDone;
        Access();
    }

    void SectorCache::Modify(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone)
    {
        Start(Request::modify, sector, rank);
        onModify = onDone;
        Access();
    }

    void SectorCache::Overwrite(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone)
    {
        Start(Request::overwrite, sector, rank);
        onModify = onDone;
        Access();
    }

    void SectorCache::WriteBack(const infra::Function<void(Result result)>& onDone)
    {
        Start(Request::writeBack, 0, 0);
        onWriteBack = onDone;
        WriteBackAll();
    }

    void SectorCache::Flush(const infra::Function<void(Result result)>& onDone)
    {
        Start(Request::flush, 0, 0);
        onWriteBack = onDone;
        WriteBackAll();
    }

    void SectorCache::Discard(uint32_t beginSector, uint32_t endSector)
    {
        really_assert(request == Request::none);

        for (auto& entry : entries)
            if (entry.valid && entry.sector >= beginSector && entry.sector < endSector)
            {
                entry.valid = false;
                entry.dirty = false;
            }
    }

    void SectorCache::Overlay(uint32_t firstSector, infra::ByteRange buffer) const
    {
        really_assert(buffer.size() % sectorSize == 0);

        auto sectors = buffer.size() / sectorSize;

        for (const auto& entry : entries)
            if (entry.valid && entry.sector >= firstSector && entry.sector - firstSector < sectors)
                infra::Copy(infra::MakeConst(BufferOf(entry)), infra::Head(infra::DiscardHead(buffer, static_cast<std::size_t>(entry.sector - firstSector) * sectorSize), sectorSize));
    }

    void SectorCache::Clear()
    {
        really_assert(request == Request::none);

        for (auto& entry : entries)
        {
            entry.valid = false;
            entry.dirty = false;
        }
    }

    void SectorCache::Start(Request newRequest, uint32_t sector, uint8_t rank)
    {
        really_assert(request == Request::none);

        request = newRequest;
        requestedSector = sector;
        requestedRank = rank;
        target = nullptr;
    }

    void SectorCache::Access()
    {
        target = Find(entries, requestedSector);

        if (target != nullptr)
            Hit();
        else
            Miss();
    }

    void SectorCache::Hit()
    {
        if (request != Request::readOnce)
            Touch(*target);

        if (request != Request::read && request != Request::readOnce && target->dirty && target->rank != requestedRank)
            WriteBackAll();
        else
            Available();
    }

    void SectorCache::Miss()
    {
        target = &SelectSlot(leastRecentlyUsed);

        if (target->valid && target->dirty && request == Request::readOnce)
            Complete(Result::busy);
        else if (target->valid && target->dirty)
        {
            writingVictim = true;
            WriteEntry(*target);
        }
        else
            Load();
    }

    void SectorCache::Load()
    {
        target->valid = false;
        target->dirty = false;
        target->sector = requestedSector;

        if (request == Request::readOnce)
            MakeLeastRecent(*target);
        else
            Touch(*target);

        if (request == Request::overwrite)
        {
            target->valid = true;
            Available();
        }
        else
            barrier.Device().ReadBlocks(BufferOf(*target), requestedSector, [this](hal::BlockDevice::Result deviceResult)
                {
                    OnLoaded(deviceResult);
                });
    }

    void SectorCache::OnLoaded(hal::BlockDevice::Result deviceResult)
    {
        if (deviceResult != hal::BlockDevice::Result::success)
            Complete(ToResult(deviceResult));
        else
        {
            target->valid = true;
            Available();
        }
    }

    void SectorCache::Available()
    {
        if (request != Request::read && request != Request::readOnce)
            MarkModified();

        Complete(Result::success);
    }

    void SectorCache::MarkModified()
    {
        if (request == Request::overwrite)
        {
            auto contents = BufferOf(*target);
            std::ranges::fill(contents, uint8_t{ 0 });
        }

        target->dirty = true;
        target->rank = requestedRank;
    }

    void SectorCache::WriteBackAll()
    {
        writingVictim = false;
        WriteNextModified();
    }

    void SectorCache::WriteNextModified()
    {
        auto next = NextModified(entries);

        if (next == nullptr)
            AllWritten();
        else
            WriteEntry(*next);
    }

    void SectorCache::AllWritten()
    {
        if (request == Request::writeBack)
            Complete(Result::success);
        else if (request == Request::flush)
            barrier.Flush([this](hal::BlockDevice::Result deviceResult)
                {
                    Complete(ToResult(deviceResult));
                });
        else
            Available();
    }

    void SectorCache::WriteEntry(Entry& entry)
    {
        writing = &entry;
        copy = 0;
        WriteCopy();
    }

    void SectorCache::WriteCopy()
    {
        barrier.Write(BufferOf(*writing), CopySector(mirroredRegion, writing->sector, copy), writing->rank, [this](hal::BlockDevice::Result deviceResult)
            {
                OnWritten(deviceResult);
            });
    }

    void SectorCache::OnWritten(hal::BlockDevice::Result deviceResult)
    {
        if (deviceResult != hal::BlockDevice::Result::success)
            Complete(ToResult(deviceResult));
        else if (++copy != Copies(mirroredRegion, writing->sector))
            WriteCopy();
        else
        {
            writing->dirty = false;

            if (writingVictim)
                Load();
            else
                WriteNextModified();
        }
    }

    void SectorCache::Complete(Result completionResult)
    {
        result = completionResult;
        infra::EventDispatcher::Instance().Schedule([this]
            {
                Deliver();
            });
    }

    void SectorCache::Deliver()
    {
        auto completed = std::exchange(request, Request::none);
        auto contents = result == Result::success && target != nullptr ? BufferOf(*target) : infra::ByteRange();

        if (completed == Request::read || completed == Request::readOnce)
            onRead(result, contents);
        else if (completed == Request::writeBack || completed == Request::flush)
            onWriteBack(result);
        else
            onModify(result, contents);
    }

    void SectorCache::Touch(Entry& entry)
    {
        leastRecentlyUsed.erase(entry);
        leastRecentlyUsed.push_back(entry);
    }

    void SectorCache::MakeLeastRecent(Entry& entry)
    {
        leastRecentlyUsed.erase(entry);
        leastRecentlyUsed.push_front(entry);
    }

    infra::ByteRange SectorCache::BufferOf(const Entry& entry) const
    {
        auto index = static_cast<std::size_t>(&entry - entries.begin());

        return infra::Head(infra::DiscardHead(buffers, index * sectorSize), sectorSize);
    }
}
