#include "validation/firmware/devices/NullBlockDevice.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>

namespace validation
{
    NullBlockDevice::NullBlockDevice(uint32_t numberOfBlocks)
        : numberOfBlocks(numberOfBlocks)
    {}

    uint32_t NullBlockDevice::BlockSize() const
    {
        return blockSize;
    }

    uint32_t NullBlockDevice::NumberOfBlocks() const
    {
        return numberOfBlocks;
    }

    void NullBlockDevice::ReadBlocks(infra::ByteRange buffer, uint32_t, const infra::Function<void(Result)>& onDone)
    {
        std::fill(buffer.begin(), buffer.end(), 0);
        Complete(onDone);
    }

    void NullBlockDevice::WriteBlocks(infra::ConstByteRange, uint32_t, const infra::Function<void(Result)>& onDone)
    {
        Complete(onDone);
    }

    void NullBlockDevice::EraseBlocks(uint32_t, uint32_t, const infra::Function<void(Result)>& onDone)
    {
        Complete(onDone);
    }

    void NullBlockDevice::Flush(const infra::Function<void(Result)>& onDone)
    {
        Complete(onDone);
    }

    void NullBlockDevice::Complete(const infra::Function<void(Result)>& onDone)
    {
        pending = onDone;
        infra::EventDispatcher::Instance().Schedule([this]
            {
                pending(Result::success);
            });
    }
}
