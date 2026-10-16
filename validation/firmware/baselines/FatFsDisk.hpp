#ifndef VALIDATION_FIRMWARE_BASELINES_FAT_FS_DISK_HPP
#define VALIDATION_FIRMWARE_BASELINES_FAT_FS_DISK_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include <cstdint>

namespace validation
{
    class FatFsDisk
        : public infra::InterfaceConnector<FatFsDisk>
    {
    public:
        FatFsDisk(hal::BlockDevice& device, uint32_t eraseBlockSectors);

        uint32_t BlockSize() const;
        uint32_t NumberOfBlocks() const;
        uint32_t EraseBlockSectors() const;

        bool Read(uint8_t* buffer, uint32_t firstBlock, uint32_t blocks);
        bool Write(const uint8_t* buffer, uint32_t firstBlock, uint32_t blocks);
        bool Flush();
        bool Erase(uint32_t beginBlock, uint32_t endBlock);

    private:
        infra::Function<void(hal::BlockDevice::Result)> Completion();
        bool Await() const;

    private:
        hal::BlockDevice& device;
        uint32_t eraseBlockSectors;
        bool done = false;
        hal::BlockDevice::Result result = hal::BlockDevice::Result::success;
    };
}

#endif
