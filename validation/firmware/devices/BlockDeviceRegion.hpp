#ifndef VALIDATION_FIRMWARE_DEVICES_BLOCK_DEVICE_REGION_HPP
#define VALIDATION_FIRMWARE_DEVICES_BLOCK_DEVICE_REGION_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include <cstdint>

namespace validation
{
    // The blocks of a device from a first block on, at most a number of them
    class BlockDeviceRegion
        : public hal::BlockDevice
    {
    public:
        BlockDeviceRegion(hal::BlockDevice& device, uint32_t firstBlock, uint32_t blocks);
        BlockDeviceRegion(const BlockDeviceRegion& other) = delete;
        BlockDeviceRegion& operator=(const BlockDeviceRegion& other) = delete;
        ~BlockDeviceRegion() = default;

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

    private:
        hal::BlockDevice& device;
        uint32_t firstBlock;
        uint32_t blocks;
    };
}

#endif
