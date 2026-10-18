#include "fs/common/FlushBarrier.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    FlushBarrier::FlushBarrier(hal::BlockDevice& device)
        : device(device)
    {}

    FlushBarrier::~FlushBarrier() = default;

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
        Transfer(writeAfterFlush ? Step::flush : Step::write);
    }

    void FlushBarrier::Flush(const infra::Function<void(hal::BlockDevice::Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->onDone = onDone;
        writeAfterFlush = false;

        if (HasUnflushedWrites())
            Transfer(Step::flush);
        else
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    this->onDone(hal::BlockDevice::Result::success);
                });
    }

    void FlushBarrier::Transfer(Step next)
    {
        auto onTransferred = [this](hal::BlockDevice::Result result)
        {
            Transferred(result);
        };

        step = next;

        if (next == Step::flush)
            device.Flush(onTransferred);
        else
            device.WriteBlocks(data, firstBlock, onTransferred);
    }

    void FlushBarrier::Transferred(hal::BlockDevice::Result result)
    {
        if (step == Step::flush)
            OnFlushed(result);
        else
            OnWritten(result);
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
            Transfer(Step::write);
        else
            onDone(result);
    }
}
