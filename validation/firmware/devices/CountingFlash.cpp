#include "validation/firmware/devices/CountingFlash.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "validation/firmware/timing/Nanoseconds.hpp"

namespace validation
{
    FlashCounts FlashCounts::operator-(const FlashCounts& earlier) const
    {
        return { readRequests - earlier.readRequests, bytesRead - earlier.bytesRead, readNs - earlier.readNs,
            programRequests - earlier.programRequests, bytesProgrammed - earlier.bytesProgrammed, pagesProgrammed - earlier.pagesProgrammed, programNs - earlier.programNs,
            eraseRequests - earlier.eraseRequests, sectorsErased - earlier.sectorsErased, eraseNs - earlier.eraseNs };
    }

    FlashCounts FlashCounts::operator+(const FlashCounts& other) const
    {
        return { readRequests + other.readRequests, bytesRead + other.bytesRead, readNs + other.readNs,
            programRequests + other.programRequests, bytesProgrammed + other.bytesProgrammed, pagesProgrammed + other.pagesProgrammed, programNs + other.programNs,
            eraseRequests + other.eraseRequests, sectorsErased + other.sectorsErased, eraseNs + other.eraseNs };
    }

    CountingFlash::CountingFlash(hal::Flash& flash, uint32_t pageSize, const CycleCounter& cycles, uint32_t clockHz, const FlashGuard& guard)
        : flash(flash)
        , pageSize(pageSize)
        , cycles(cycles)
        , clockHz(clockHz)
        , guard(guard)
    {
        really_assert(pageSize != 0 && clockHz != 0);
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
        this->onTimeout = onTimeout;
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
        return lastCycles;
    }

    bool CountingFlash::Busy() const
    {
        return static_cast<bool>(onDone);
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
        requestNs = &totalNs;
        guardTimer.Start(guardDuration, [this]
            {
                if (onTimeout)
                    onTimeout();
            });
        startCycles = cycles.Cycles();

        return [this]
        {
            Completed();
        };
    }

    void CountingFlash::Completed()
    {
        lastCycles = cycles.Cycles() - startCycles;
        guardTimer.Cancel();
        *requestNs += Nanoseconds(lastCycles, clockHz);
        onDone();
    }
}
