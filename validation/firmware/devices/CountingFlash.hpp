#ifndef VALIDATION_FIRMWARE_DEVICES_COUNTING_FLASH_HPP
#define VALIDATION_FIRMWARE_DEVICES_COUNTING_FLASH_HPP

#include "hal/interfaces/Flash.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/devices/RequestMeter.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include <cstdint>

namespace validation
{
    struct FlashCounts
    {
        uint64_t readRequests{};
        uint64_t bytesRead{};
        uint64_t readNs{};
        uint64_t programRequests{};
        uint64_t bytesProgrammed{};
        uint64_t pagesProgrammed{};
        uint64_t programNs{};
        uint64_t eraseRequests{};
        uint64_t sectorsErased{};
        uint64_t eraseNs{};

        bool operator==(const FlashCounts& other) const = default;

        friend FlashCounts operator-(const FlashCounts& later, const FlashCounts& earlier)
        {
            return { later.readRequests - earlier.readRequests, later.bytesRead - earlier.bytesRead, later.readNs - earlier.readNs,
                later.programRequests - earlier.programRequests, later.bytesProgrammed - earlier.bytesProgrammed, later.pagesProgrammed - earlier.pagesProgrammed, later.programNs - earlier.programNs,
                later.eraseRequests - earlier.eraseRequests, later.sectorsErased - earlier.sectorsErased, later.eraseNs - earlier.eraseNs };
        }

        friend FlashCounts operator+(const FlashCounts& left, const FlashCounts& right)
        {
            return { left.readRequests + right.readRequests, left.bytesRead + right.bytesRead, left.readNs + right.readNs,
                left.programRequests + right.programRequests, left.bytesProgrammed + right.bytesProgrammed, left.pagesProgrammed + right.pagesProgrammed, left.programNs + right.programNs,
                left.eraseRequests + right.eraseRequests, left.sectorsErased + right.sectorsErased, left.eraseNs + right.eraseNs };
        }
    };

    struct FlashGuard
    {
        infra::Duration read;
        infra::Duration page;
        infra::Duration sector;
    };

    class CountingFlash
        : public hal::Flash
    {
    public:
        CountingFlash(hal::Flash& flash, uint32_t pageSize, const CycleCounter& cycles, uint32_t clockHz, const FlashGuard& guard);
        CountingFlash(const CountingFlash& other) = delete;
        CountingFlash& operator=(const CountingFlash& other) = delete;
        ~CountingFlash() = default;

        uint32_t NumberOfSectors() const override;
        uint32_t SizeOfSector(uint32_t sectorIndex) const override;
        uint32_t SectorOfAddress(uint32_t address) const override;
        uint32_t AddressOfSector(uint32_t sectorIndex) const override;

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

        void OnTimeout(const infra::Function<void()>& onTimeout);
        RequestMeter& Meter();

        uint32_t PageSize() const;
        const FlashCounts& Counts() const;
        uint32_t LastCycles() const;
        bool Busy() const;
        bool InUse() const;

    private:
        uint32_t PagesSpanned(uint32_t address, std::size_t size) const;
        infra::Function<void()> Start(uint64_t& totalNs, infra::Duration guardDuration, const infra::Function<void()>& onDone);
        void Completed();

    private:
        hal::Flash& flash;
        uint32_t pageSize;
        FlashGuard guard;
        FlashCounts counts;
        RequestMeter meter;
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
