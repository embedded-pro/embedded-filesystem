#ifndef METRICS_BASELINES_SYNCHRONOUS_DEVICES_HPP
#define METRICS_BASELINES_SYNCHRONOUS_DEVICES_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "hal/interfaces/Flash.hpp"
#include "metrics/device/VirtualTime.hpp"
#include <optional>

namespace metrics
{
    class SynchronousFlash
    {
    public:
        SynchronousFlash(VirtualTime& time, hal::Flash& flash);
        SynchronousFlash(const SynchronousFlash& other) = delete;
        SynchronousFlash& operator=(const SynchronousFlash& other) = delete;

        uint32_t SectorSize() const;
        uint32_t NumberOfSectors() const;

        bool Read(infra::ByteRange buffer, uint32_t address);
        bool Write(infra::ConstByteRange buffer, uint32_t address);
        bool EraseSector(uint32_t sector);

    private:
        bool WaitFor(const bool& done);

    private:
        VirtualTime& time;
        hal::Flash& flash;
    };

    class SynchronousBlockDevice
    {
    public:
        using Result = hal::BlockDevice::Result;

        SynchronousBlockDevice(VirtualTime& time, hal::BlockDevice& device);
        SynchronousBlockDevice(const SynchronousBlockDevice& other) = delete;
        SynchronousBlockDevice& operator=(const SynchronousBlockDevice& other) = delete;

        uint32_t BlockSize() const;
        uint32_t NumberOfBlocks() const;

        Result Read(infra::ByteRange buffer, uint32_t firstBlock);
        Result Write(infra::ConstByteRange buffer, uint32_t firstBlock);
        Result Discard(uint32_t beginBlock, uint32_t endBlock);
        Result Flush();

    private:
        Result WaitFor(const std::optional<Result>& result);

    private:
        VirtualTime& time;
        hal::BlockDevice& device;
    };
}

#endif
