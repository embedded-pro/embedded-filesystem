#include "validation/firmware/devices/RamBlockDevice.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace validation
{
    RamBlockDevice::RamBlockDevice(infra::ByteRange storage)
        : storage(storage)
    {
        really_assert(storage.size() % blockSize == 0);
        std::fill(storage.begin(), storage.end(), 0);
    }

    uint32_t RamBlockDevice::BlockSize() const
    {
        return blockSize;
    }

    uint32_t RamBlockDevice::NumberOfBlocks() const
    {
        return static_cast<uint32_t>(storage.size() / blockSize);
    }

    void RamBlockDevice::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        really_assert(buffer.size() % blockSize == 0);

        if (!IsWithin(firstBlock, buffer.size()))
            Complete(onDone, Result::outOfRange);
        else
        {
            std::copy_n(storage.begin() + firstBlock * blockSize, buffer.size(), buffer.begin());
            Complete(onDone, Result::success);
        }
    }

    void RamBlockDevice::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        really_assert(buffer.size() % blockSize == 0);

        if (!IsWithin(firstBlock, buffer.size()))
            Complete(onDone, Result::outOfRange);
        else
        {
            std::copy(buffer.begin(), buffer.end(), storage.begin() + firstBlock * blockSize);
            Complete(onDone, Result::success);
        }
    }

    void RamBlockDevice::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        if (beginBlock > endBlock || !IsWithin(beginBlock, std::size_t{ endBlock - beginBlock } * blockSize))
            Complete(onDone, Result::outOfRange);
        else
        {
            std::fill(storage.begin() + beginBlock * blockSize, storage.begin() + endBlock * blockSize, 0);
            Complete(onDone, Result::success);
        }
    }

    void RamBlockDevice::Flush(const infra::Function<void(Result)>& onDone)
    {
        Complete(onDone, Result::success);
    }

    bool RamBlockDevice::IsWithin(uint32_t beginBlock, std::size_t bytes) const
    {
        return beginBlock <= NumberOfBlocks() && bytes <= storage.size() - std::size_t{ beginBlock } * blockSize;
    }

    void RamBlockDevice::Complete(const infra::Function<void(Result)>& onDone, Result result)
    {
        pending = onDone;
        pendingResult = result;
        infra::EventDispatcher::Instance().Schedule([this]
            {
                pending(pendingResult);
            });
    }
}
