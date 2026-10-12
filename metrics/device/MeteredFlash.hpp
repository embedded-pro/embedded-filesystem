#ifndef METRICS_DEVICE_METERED_FLASH_HPP
#define METRICS_DEVICE_METERED_FLASH_HPP

#include "hal/interfaces/Flash.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/device/FlashStatistics.hpp"
#include <vector>

namespace metrics
{
    class MeteredFlash
        : public hal::Flash
    {
    public:
        MeteredFlash(hal::Flash& flash, const NorModel& model, TimingCase timingCase = TimingCase::typical);

        uint32_t NumberOfSectors() const override;
        uint32_t SizeOfSector(uint32_t sectorIndex) const override;
        uint32_t SectorOfAddress(uint32_t address) const override;
        uint32_t AddressOfSector(uint32_t sectorIndex) const override;

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

        const FlashCounters& Counters() const;
        const std::vector<uint32_t>& EraseCounts() const;
        const NorModel& Model() const;
        bool Busy() const;

    private:
        void Begin(const infra::Function<void()>& onDone, infra::Duration deviceTime);
        void WrappedDone();
        void AssertRange(uint32_t address, std::size_t size) const;
        void TrackProgram(uint32_t address, std::size_t size);
        void TrackErase(uint32_t beginIndex, uint32_t endIndex);

    private:
        hal::Flash& flash;
        const NorModel& model;
        TimingCase timingCase;
        uint32_t totalSize;
        FlashCounters counters;
        std::vector<uint32_t> eraseCounts;
        std::vector<bool> programmed;
        infra::AutoResetFunction<void()> onDone;
        infra::Duration pendingDeviceTime{};
        infra::TimerSingleShot completionTimer;
    };
}

#endif
