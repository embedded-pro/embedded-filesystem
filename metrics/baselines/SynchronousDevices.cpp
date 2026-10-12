#include "metrics/baselines/SynchronousDevices.hpp"

namespace metrics
{
    SynchronousFlash::SynchronousFlash(VirtualTime& time, hal::Flash& flash)
        : time(time)
        , flash(flash)
    {}

    uint32_t SynchronousFlash::SectorSize() const
    {
        return flash.SizeOfSector(0);
    }

    uint32_t SynchronousFlash::NumberOfSectors() const
    {
        return flash.NumberOfSectors();
    }

    bool SynchronousFlash::Read(infra::ByteRange buffer, uint32_t address)
    {
        bool done = false;
        flash.ReadBuffer(buffer, address, [&done]
            {
                done = true;
            });
        return WaitFor(done);
    }

    bool SynchronousFlash::Write(infra::ConstByteRange buffer, uint32_t address)
    {
        bool done = false;
        flash.WriteBuffer(buffer, address, [&done]
            {
                done = true;
            });
        return WaitFor(done);
    }

    bool SynchronousFlash::EraseSector(uint32_t sector)
    {
        bool done = false;
        flash.EraseSector(sector, [&done]
            {
                done = true;
            });
        return WaitFor(done);
    }

    bool SynchronousFlash::WaitFor(const bool& done)
    {
        return time.RunUntil([&done]
            {
                return done;
            });
    }

    SynchronousBlockDevice::SynchronousBlockDevice(VirtualTime& time, hal::BlockDevice& device)
        : time(time)
        , device(device)
    {}

    uint32_t SynchronousBlockDevice::BlockSize() const
    {
        return device.BlockSize();
    }

    uint32_t SynchronousBlockDevice::NumberOfBlocks() const
    {
        return device.NumberOfBlocks();
    }

    SynchronousBlockDevice::Result SynchronousBlockDevice::Read(infra::ByteRange buffer, uint32_t firstBlock)
    {
        std::optional<Result> result;
        device.ReadBlocks(buffer, firstBlock, [&result](Result completed)
            {
                result = completed;
            });
        return WaitFor(result);
    }

    SynchronousBlockDevice::Result SynchronousBlockDevice::Write(infra::ConstByteRange buffer, uint32_t firstBlock)
    {
        std::optional<Result> result;
        device.WriteBlocks(buffer, firstBlock, [&result](Result completed)
            {
                result = completed;
            });
        return WaitFor(result);
    }

    SynchronousBlockDevice::Result SynchronousBlockDevice::Discard(uint32_t beginBlock, uint32_t endBlock)
    {
        std::optional<Result> result;
        device.EraseBlocks(beginBlock, endBlock, [&result](Result completed)
            {
                result = completed;
            });
        return WaitFor(result);
    }

    SynchronousBlockDevice::Result SynchronousBlockDevice::Flush()
    {
        std::optional<Result> result;
        device.Flush([&result](Result completed)
            {
                result = completed;
            });
        return WaitFor(result);
    }

    SynchronousBlockDevice::Result SynchronousBlockDevice::WaitFor(const std::optional<Result>& result)
    {
        if (!time.RunUntil([&result]
                {
                    return result.has_value();
                }))
            return Result::timeout;

        return *result;
    }
}
