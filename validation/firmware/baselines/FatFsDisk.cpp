#include "validation/firmware/baselines/FatFsDisk.hpp"
#include "infra/event/EventDispatcher.hpp"
// ff.h includes windows.h on Windows, so it follows the headers of EmIL
#include "ff.h"
// diskio.h uses the types that ff.h defines
#include "diskio.h"

namespace validation
{
    FatFsDisk::FatFsDisk(hal::BlockDevice& device, uint32_t eraseBlockSectors)
        : device(device)
        , eraseBlockSectors(eraseBlockSectors)
    {}

    uint32_t FatFsDisk::BlockSize() const
    {
        return device.BlockSize();
    }

    uint32_t FatFsDisk::NumberOfBlocks() const
    {
        return device.NumberOfBlocks();
    }

    uint32_t FatFsDisk::EraseBlockSectors() const
    {
        return eraseBlockSectors;
    }

    bool FatFsDisk::Read(uint8_t* buffer, uint32_t firstBlock, uint32_t blocks)
    {
        device.ReadBlocks(infra::ByteRange(buffer, buffer + blocks * device.BlockSize()), firstBlock, Completion());
        return Await();
    }

    bool FatFsDisk::Write(const uint8_t* buffer, uint32_t firstBlock, uint32_t blocks)
    {
        device.WriteBlocks(infra::ConstByteRange(buffer, buffer + blocks * device.BlockSize()), firstBlock, Completion());
        return Await();
    }

    bool FatFsDisk::Flush()
    {
        device.Flush(Completion());
        return Await();
    }

    bool FatFsDisk::Erase(uint32_t beginBlock, uint32_t endBlock)
    {
        device.EraseBlocks(beginBlock, endBlock, Completion());
        return Await();
    }

    infra::Function<void(hal::BlockDevice::Result)> FatFsDisk::Completion()
    {
        done = false;
        return [this](hal::BlockDevice::Result completed)
        {
            result = completed;
            done = true;
        };
    }

    bool FatFsDisk::Await() const
    {
        infra::EventDispatcher::Instance().ExecuteUntil([this]
            {
                return done;
            });

        return result == hal::BlockDevice::Result::success;
    }
}

namespace
{
    DRESULT Status(bool succeeded)
    {
        return succeeded ? RES_OK : RES_ERROR;
    }

    DRESULT EraseBlockSize(const validation::FatFsDisk& disk, void* buffer)
    {
        if (disk.EraseBlockSectors() == 0)
            return RES_PARERR;

        *static_cast<DWORD*>(buffer) = disk.EraseBlockSectors();
        return RES_OK;
    }

    DRESULT Trim(validation::FatFsDisk& disk, const void* buffer)
    {
        const auto* range = static_cast<const LBA_t*>(buffer);
        return Status(disk.Erase(static_cast<uint32_t>(range[0]), static_cast<uint32_t>(range[1] + 1)));
    }
}

extern "C"
{
    DSTATUS disk_initialize(BYTE)
    {
        return validation::FatFsDisk::InstanceSet() ? 0 : STA_NOINIT;
    }

    DSTATUS disk_status(BYTE)
    {
        return validation::FatFsDisk::InstanceSet() ? 0 : STA_NOINIT;
    }

    DRESULT disk_read(BYTE, BYTE* buffer, LBA_t sector, UINT count)
    {
        return Status(validation::FatFsDisk::Instance().Read(buffer, static_cast<uint32_t>(sector), count));
    }

    DRESULT disk_write(BYTE, const BYTE* buffer, LBA_t sector, UINT count)
    {
        return Status(validation::FatFsDisk::Instance().Write(buffer, static_cast<uint32_t>(sector), count));
    }

    DRESULT disk_ioctl(BYTE, BYTE command, void* buffer)
    {
        auto& disk = validation::FatFsDisk::Instance();

        switch (command)
        {
            case CTRL_SYNC:
                return Status(disk.Flush());
            case GET_SECTOR_COUNT:
                *static_cast<LBA_t*>(buffer) = disk.NumberOfBlocks();
                return RES_OK;
            case GET_SECTOR_SIZE:
                *static_cast<WORD*>(buffer) = static_cast<WORD>(disk.BlockSize());
                return RES_OK;
            case GET_BLOCK_SIZE:
                return EraseBlockSize(disk, buffer);
            case CTRL_TRIM:
                return Trim(disk, buffer);
            default:
                return RES_PARERR;
        }
    }
}
