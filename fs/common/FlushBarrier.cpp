#include "fs/common/FlushBarrier.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    FlushBarrier::FlushBarrier(hal::BlockDevice& device)
        : device(device)
    {}

    hal::BlockDevice& FlushBarrier::Device() const
    {
        return device;
    }

    bool FlushBarrier::HasUnflushedWrites() const
    {
        return lowestUnflushedRank.has_value();
    }

    void FlushBarrier::Write(infra::ConstByteRange data, uint32_t firstBlock, uint8_t rank, const infra::Function<void(hal::BlockDevice::Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->data = data;
        this->firstBlock = firstBlock;
        this->rank = rank;
        this->onDone = onDone;
        writeAfterFlush = lowestUnflushedRank.has_value() && *lowestUnflushedRank < rank;

        if (writeAfterFlush)
            device.Flush([this](hal::BlockDevice::Result result)
                {
                    OnFlushed(result);
                });
        else
            WriteNow();
    }

    void FlushBarrier::Flush(const infra::Function<void(hal::BlockDevice::Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->onDone = onDone;
        writeAfterFlush = false;

        if (HasUnflushedWrites())
            device.Flush([this](hal::BlockDevice::Result result)
                {
                    OnFlushed(result);
                });
        else
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    this->onDone(hal::BlockDevice::Result::success);
                });
    }

    void FlushBarrier::WriteNow()
    {
        device.WriteBlocks(data, firstBlock, [this](hal::BlockDevice::Result result)
            {
                OnWritten(result);
            });
    }

    void FlushBarrier::OnWritten(hal::BlockDevice::Result result)
    {
        lowestUnflushedRank = std::min(lowestUnflushedRank.value_or(rank), rank);
        onDone(result);
    }

    void FlushBarrier::OnFlushed(hal::BlockDevice::Result result)
    {
        if (result == hal::BlockDevice::Result::success)
            lowestUnflushedRank = std::nullopt;

        if (writeAfterFlush && result == hal::BlockDevice::Result::success)
            WriteNow();
        else
            onDone(result);
    }
}
