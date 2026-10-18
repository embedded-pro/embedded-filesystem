#include "validation/firmware/devices/BlockDeviceRegion.hpp"
#include <algorithm>

namespace validation
{
    BlockDeviceRegion::BlockDeviceRegion(hal::BlockDevice& device, uint32_t firstBlock, uint32_t blocks)
        : device(device)
        , firstBlock(firstBlock)
        , blocks(blocks)
    {}

    uint32_t BlockDeviceRegion::BlockSize() const
    {
        return device.BlockSize();
    }

    uint32_t BlockDeviceRegion::NumberOfBlocks() const
    {
        auto deviceBlocks = device.NumberOfBlocks();
        return deviceBlocks > firstBlock ? std::min(blocks, deviceBlocks - firstBlock) : 0;
    }

    void BlockDeviceRegion::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        device.ReadBlocks(buffer, this->firstBlock + firstBlock, onDone);
    }

    void BlockDeviceRegion::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        device.WriteBlocks(buffer, this->firstBlock + firstBlock, onDone);
    }

    void BlockDeviceRegion::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        device.EraseBlocks(firstBlock + beginBlock, firstBlock + endBlock, onDone);
    }

    void BlockDeviceRegion::Flush(const infra::Function<void(Result)>& onDone)
    {
        device.Flush(onDone);
    }
}
