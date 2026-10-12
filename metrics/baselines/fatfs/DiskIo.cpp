#include "metrics/baselines/fatfs/DiskIo.hpp"
#include "diskio.h"

namespace metrics
{
    namespace
    {
        bool Succeeded(SynchronousBlockDevice::Result result)
        {
            return result == SynchronousBlockDevice::Result::success;
        }

        DRESULT Status(SynchronousBlockDevice::Result result)
        {
            return Succeeded(result) ? RES_OK : RES_ERROR;
        }

        DRESULT EraseBlockSize(void* buffer)
        {
            if (DiskIo().eraseBlockSectors == 0)
                return RES_PARERR;

            *static_cast<DWORD*>(buffer) = DiskIo().eraseBlockSectors;
            return RES_OK;
        }

        DRESULT Trim(const void* buffer)
        {
            const auto* range = static_cast<const LBA_t*>(buffer);
            return Status(DiskIo().device->Discard(static_cast<uint32_t>(range[0]), static_cast<uint32_t>(range[1] + 1)));
        }
    }

    DiskIoBinding& DiskIo()
    {
        static DiskIoBinding binding;
        return binding;
    }
}

extern "C"
{
    DSTATUS disk_initialize(BYTE)
    {
        return metrics::DiskIo().device != nullptr ? 0 : STA_NOINIT;
    }

    DSTATUS disk_status(BYTE)
    {
        return metrics::DiskIo().device != nullptr ? 0 : STA_NOINIT;
    }

    DRESULT disk_read(BYTE, BYTE* buffer, LBA_t sector, UINT count)
    {
        auto& device = *metrics::DiskIo().device;
        return metrics::Status(device.Read(infra::ByteRange(buffer, buffer + count * device.BlockSize()), static_cast<uint32_t>(sector)));
    }

    DRESULT disk_write(BYTE, const BYTE* buffer, LBA_t sector, UINT count)
    {
        auto& device = *metrics::DiskIo().device;
        return metrics::Status(device.Write(infra::ConstByteRange(buffer, buffer + count * device.BlockSize()), static_cast<uint32_t>(sector)));
    }

    DRESULT disk_ioctl(BYTE, BYTE command, void* buffer)
    {
        auto& device = *metrics::DiskIo().device;

        switch (command)
        {
            case CTRL_SYNC:
                return metrics::Status(device.Flush());
            case GET_SECTOR_COUNT:
                *static_cast<LBA_t*>(buffer) = device.NumberOfBlocks();
                return RES_OK;
            case GET_SECTOR_SIZE:
                *static_cast<WORD*>(buffer) = static_cast<WORD>(device.BlockSize());
                return RES_OK;
            case GET_BLOCK_SIZE:
                return metrics::EraseBlockSize(buffer);
            case CTRL_TRIM:
                return metrics::Trim(buffer);
            default:
                return RES_PARERR;
        }
    }
}
