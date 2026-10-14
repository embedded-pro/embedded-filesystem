#include "metrics/device/MeteredFlash.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace metrics
{
    namespace
    {
        uint32_t TotalSizeOf(const hal::Flash& flash)
        {
            uint32_t size = 0;
            for (uint32_t sector = 0; sector != flash.NumberOfSectors(); ++sector)
                size += flash.SizeOfSector(sector);

            return size;
        }
    }

    MeteredFlash::MeteredFlash(hal::Flash& flash, const NorModel& model, TimingCase timingCase)
        : flash(flash)
        , model(model)
        , timingCase(timingCase)
        , totalSize(TotalSizeOf(flash))
        , eraseCounts(flash.NumberOfSectors(), 0)
        , programmed((totalSize + model.programUnit - 1) / model.programUnit, false)
    {}

    uint32_t MeteredFlash::NumberOfSectors() const
    {
        return flash.NumberOfSectors();
    }

    uint32_t MeteredFlash::SizeOfSector(uint32_t sectorIndex) const
    {
        return flash.SizeOfSector(sectorIndex);
    }

    uint32_t MeteredFlash::SectorOfAddress(uint32_t address) const
    {
        return flash.SectorOfAddress(address);
    }

    uint32_t MeteredFlash::AddressOfSector(uint32_t sectorIndex) const
    {
        return flash.AddressOfSector(sectorIndex);
    }

    void MeteredFlash::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        auto deviceTime = ProgramTime(model, timingCase, address, buffer.size());
        Begin(onDone, deviceTime);
        AssertRange(address, buffer.size());

        ++counters.programRequests;
        counters.bytesProgrammed += buffer.size();
        counters.programTime += deviceTime;
        TrackProgram(address, buffer.size());

        flash.WriteBuffer(buffer, address, [this]
            {
                WrappedDone();
            });
    }

    void MeteredFlash::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        auto deviceTime = ReadTime(model, timingCase, buffer.size());
        Begin(onDone, deviceTime);
        AssertRange(address, buffer.size());

        ++counters.readRequests;
        counters.bytesRead += buffer.size();
        counters.readTime += deviceTime;

        flash.ReadBuffer(buffer, address, [this]
            {
                WrappedDone();
            });
    }

    void MeteredFlash::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        really_assert(beginIndex <= endIndex && endIndex <= NumberOfSectors());
        auto deviceTime = EraseTime(model, timingCase, endIndex - beginIndex);
        Begin(onDone, deviceTime);

        ++counters.eraseRequests;
        counters.sectorsErased += endIndex - beginIndex;
        counters.eraseTime += deviceTime;
        TrackErase(beginIndex, endIndex);

        flash.EraseSectors(beginIndex, endIndex, [this]
            {
                WrappedDone();
            });
    }

    void MeteredFlash::MarkProgrammed(uint32_t address, std::size_t size)
    {
        AssertRange(address, size);

        if (size != 0)
            std::fill(programmed.begin() + address / model.programUnit, programmed.begin() + (address + size - 1) / model.programUnit + 1, true);
    }

    const FlashCounters& MeteredFlash::Counters() const
    {
        return counters;
    }

    const std::vector<uint32_t>& MeteredFlash::EraseCounts() const
    {
        return eraseCounts;
    }

    const NorModel& MeteredFlash::Model() const
    {
        return model;
    }

    bool MeteredFlash::Busy() const
    {
        return static_cast<bool>(onDone);
    }

    void MeteredFlash::Begin(const infra::Function<void()>& onDone, infra::Duration deviceTime)
    {
        really_assert(!Busy());
        this->onDone = onDone;
        pendingDeviceTime = deviceTime;
    }

    void MeteredFlash::WrappedDone()
    {
        completionTimer.Start(pendingDeviceTime, [this]
            {
                infra::EventDispatcher::Instance().Schedule([this]
                    {
                        onDone();
                    });
            });
    }

    void MeteredFlash::AssertRange(uint32_t address, std::size_t size) const
    {
        really_assert(address <= totalSize && size <= totalSize - address);
    }

    void MeteredFlash::TrackProgram(uint32_t address, std::size_t size)
    {
        if (size == 0)
            return;

        auto begin = programmed.begin() + address / model.programUnit;
        auto end = programmed.begin() + (address + size - 1) / model.programUnit + 1;
        counters.programUnitRewrites += std::count(begin, end, true);
        std::fill(begin, end, true);
    }

    void MeteredFlash::TrackErase(uint32_t beginIndex, uint32_t endIndex)
    {
        for (auto sector = beginIndex; sector != endIndex; ++sector)
            ++eraseCounts[sector];

        std::fill(programmed.begin() + AddressOfSector(beginIndex) / model.programUnit, programmed.begin() + AddressOfSector(endIndex) / model.programUnit, false);
    }
}
