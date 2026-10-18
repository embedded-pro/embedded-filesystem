#include "validation/firmware/devices/CountingFlash.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace validation
{
    CountingFlash::CountingFlash(hal::Flash& flash, uint32_t pageSize, const CycleCounter& cycles, uint32_t clockHz, const FlashGuard& guard)
        : flash(flash)
        , pageSize(pageSize)
        , guard(guard)
        , meter(cycles, clockHz)
    {
        really_assert(pageSize != 0);
    }

    uint32_t CountingFlash::NumberOfSectors() const
    {
        return flash.NumberOfSectors();
    }

    uint32_t CountingFlash::SizeOfSector(uint32_t sectorIndex) const
    {
        return flash.SizeOfSector(sectorIndex);
    }

    uint32_t CountingFlash::SectorOfAddress(uint32_t address) const
    {
        return flash.SectorOfAddress(address);
    }

    uint32_t CountingFlash::AddressOfSector(uint32_t sectorIndex) const
    {
        return flash.AddressOfSector(sectorIndex);
    }

    void CountingFlash::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        auto pages = PagesSpanned(address, buffer.size());
        ++counts.programRequests;
        counts.bytesProgrammed += buffer.size();
        counts.pagesProgrammed += pages;
        flash.WriteBuffer(buffer, address, Start(counts.programNs, guard.page * pages, onDone));
    }

    void CountingFlash::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        ++counts.readRequests;
        counts.bytesRead += buffer.size();
        flash.ReadBuffer(buffer, address, Start(counts.readNs, guard.read, onDone));
    }

    void CountingFlash::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        ++counts.eraseRequests;
        counts.sectorsErased += endIndex - beginIndex;
        flash.EraseSectors(beginIndex, endIndex, Start(counts.eraseNs, guard.sector * (endIndex - beginIndex), onDone));
    }

    void CountingFlash::OnTimeout(const infra::Function<void()>& onTimeout)
    {
        meter.OnTimeout(onTimeout);
    }

    uint32_t CountingFlash::PageSize() const
    {
        return pageSize;
    }

    const FlashCounts& CountingFlash::Counts() const
    {
        return counts;
    }

    uint32_t CountingFlash::LastCycles() const
    {
        return meter.LastCycles();
    }

    bool CountingFlash::Busy() const
    {
        return static_cast<bool>(onDone);
    }

    bool CountingFlash::InUse() const
    {
        return Busy() || meter.TimeoutWatched();
    }

    uint32_t CountingFlash::PagesSpanned(uint32_t address, std::size_t size) const
    {
        if (size == 0)
            return 0;

        return static_cast<uint32_t>((address + size - 1) / pageSize - address / pageSize + 1);
    }

    infra::Function<void()> CountingFlash::Start(uint64_t& totalNs, infra::Duration guardDuration, const infra::Function<void()>& onDone)
    {
        really_assert(!Busy());

        this->onDone = onDone;
        meter.Start(totalNs, guardDuration);

        return [this]
        {
            Completed();
        };
    }

    void CountingFlash::Completed()
    {
        meter.Stop();
        onDone();
    }
}
