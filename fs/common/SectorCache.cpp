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
        uint32_t SectorSizeOf(infra::MemoryRange<SectorCache::Entry> entries, infra::ByteRange buffers)
        {
            really_assert(!entries.empty() && buffers.size() % entries.size() == 0 && buffers.size() >= entries.size());

            return static_cast<uint32_t>(buffers.size() / entries.size());
        }
    }

    SectorCache::SectorCache(hal::BlockDevice& device, infra::MemoryRange<Entry> entries, infra::ByteRange buffers)
        : device(device)
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
                infra::Copy(infra::MakeConst(BufferOf(entry)), infra::Head(infra::DiscardHead(buffer, (entry.sector - firstSector) * sectorSize), sectorSize));
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
        target = Find(requestedSector);

        if (target != nullptr)
            Hit();
        else
            Miss();
    }

    void SectorCache::Hit()
    {
        Touch(*target);

        if (request != Request::read && target->dirty && target->rank != requestedRank)
            WriteBackAll();
        else
            Available();
    }

    void SectorCache::Miss()
    {
        target = &SelectSlot();

        if (target->valid && target->dirty)
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
        Touch(*target);

        if (request == Request::overwrite)
        {
            target->valid = true;
            Available();
        }
        else
            device.ReadBlocks(BufferOf(*target), requestedSector, [this](hal::BlockDevice::Result deviceResult)
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
        if (request != Request::read)
            MarkModified();

        Complete(Result::success);
    }

    void SectorCache::MarkModified()
    {
        if (request == Request::overwrite)
        {
            auto contents = BufferOf(*target);
            std::fill(contents.begin(), contents.end(), 0);
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
        auto next = NextModified();

        if (next == nullptr)
            AllWritten();
        else
            WriteEntry(*next);
    }

    void SectorCache::AllWritten()
    {
        if (request == Request::writeBack)
            Complete(Result::success);
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
        device.WriteBlocks(BufferOf(*writing), CopySector(*writing, copy), [this](hal::BlockDevice::Result deviceResult)
            {
                OnWritten(deviceResult);
            });
    }

    void SectorCache::OnWritten(hal::BlockDevice::Result deviceResult)
    {
        if (deviceResult != hal::BlockDevice::Result::success)
            Complete(ToResult(deviceResult));
        else if (++copy != Copies(*writing))
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

        if (completed == Request::read)
            onRead(result, contents);
        else if (completed == Request::writeBack)
            onWriteBack(result);
        else
            onModify(result, contents);
    }

    SectorCache::Entry* SectorCache::Find(uint32_t sector)
    {
        for (auto& entry : entries)
            if (entry.valid && entry.sector == sector)
                return &entry;

        return nullptr;
    }

    SectorCache::Entry& SectorCache::SelectSlot()
    {
        Entry* clean = nullptr;
        Entry* modified = nullptr;

        for (auto& entry : leastRecentlyUsed)
        {
            if (!entry.valid)
                return entry;

            if (!entry.dirty && clean == nullptr)
                clean = &entry;

            if (entry.dirty && (modified == nullptr || entry.rank < modified->rank))
                modified = &entry;
        }

        return clean != nullptr ? *clean : *modified;
    }

    SectorCache::Entry* SectorCache::NextModified()
    {
        Entry* next = nullptr;

        for (auto& entry : entries)
            if (entry.dirty && (next == nullptr || std::tie(entry.rank, entry.sector) < std::tie(next->rank, next->sector)))
                next = &entry;

        return next;
    }

    void SectorCache::Touch(Entry& entry)
    {
        leastRecentlyUsed.erase(entry);
        leastRecentlyUsed.push_back(entry);
    }

    uint8_t SectorCache::Copies(const Entry& entry) const
    {
        if (mirroredRegion && entry.sector >= mirroredRegion->firstSector && entry.sector - mirroredRegion->firstSector < mirroredRegion->sectors)
            return mirroredRegion->copies;

        return 1;
    }

    uint32_t SectorCache::CopySector(const Entry& entry, uint8_t copyIndex) const
    {
        if (copyIndex == 0)
            return entry.sector;

        return entry.sector + copyIndex * mirroredRegion->sectors;
    }

    infra::ByteRange SectorCache::BufferOf(const Entry& entry) const
    {
        auto index = static_cast<std::size_t>(&entry - entries.begin());

        return infra::Head(infra::DiscardHead(buffers, index * sectorSize), sectorSize);
    }
}
