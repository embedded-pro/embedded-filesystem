#include "validation/firmware/devices/CountingBlockDevice.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace validation
{
    CountingBlockDevice::CountingBlockDevice(hal::BlockDevice& device, uint32_t unitBlocks, const CycleCounter& cycles, uint32_t clockHz, infra::Duration guard)
        : device(device)
        , unitBlocks(unitBlocks)
        , guard(guard)
        , meter(cycles, clockHz)
    {
        really_assert(unitBlocks != 0);
    }

    uint32_t CountingBlockDevice::BlockSize() const
    {
        return device.BlockSize();
    }

    uint32_t CountingBlockDevice::NumberOfBlocks() const
    {
        return device.NumberOfBlocks();
    }

    void CountingBlockDevice::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        ++counts.readRequests;
        counts.blocksRead += Blocks(buffer);
        device.ReadBlocks(buffer, firstBlock, Start(counts.readNs, onDone));
    }

    void CountingBlockDevice::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        CountWrite(firstBlock, Blocks(buffer));
        device.WriteBlocks(buffer, firstBlock, Start(counts.writeNs, onDone));
    }

    void CountingBlockDevice::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        ++counts.discardRequests;
        counts.blocksDiscarded += endBlock - beginBlock;
        device.EraseBlocks(beginBlock, endBlock, Start(counts.discardNs, onDone));
    }

    void CountingBlockDevice::Flush(const infra::Function<void(Result)>& onDone)
    {
        ++counts.flushRequests;
        device.Flush(Start(counts.flushNs, onDone));
    }

    DeviceCounts CountingBlockDevice::Snapshot() const
    {
        return { {}, counts };
    }

    void CountingBlockDevice::StartPhase()
    {
        written.reset();
    }

    void CountingBlockDevice::OnTimeout(const infra::Function<void()>& onTimeout)
    {
        meter.OnTimeout(onTimeout);
    }

    RequestMeter& CountingBlockDevice::Meter()
    {
        return meter;
    }

    uint32_t CountingBlockDevice::UnitBlocks() const
    {
        return unitBlocks;
    }

    const BlockCounts& CountingBlockDevice::Counts() const
    {
        return counts;
    }

    uint32_t CountingBlockDevice::LastCycles() const
    {
        return meter.LastCycles();
    }

    bool CountingBlockDevice::Busy() const
    {
        return static_cast<bool>(onDone);
    }

    bool CountingBlockDevice::InUse() const
    {
        return Busy() || meter.TimeoutWatched();
    }

    uint32_t CountingBlockDevice::Blocks(infra::ConstByteRange buffer) const
    {
        return static_cast<uint32_t>(buffer.size() / BlockSize());
    }

    void CountingBlockDevice::CountWrite(uint32_t firstBlock, uint32_t blocks)
    {
        auto continues = firstBlock % unitBlocks == 0 || firstBlock == nextSequentialBlock;
        counts.alignedBlocks += continues || blocks == 0 ? blocks : blocks - 1;
        nextSequentialBlock = firstBlock + blocks;
        ++counts.writeRequests;
        counts.blocksWritten += blocks;

        if (blocks != 0)
            for (auto unit = firstBlock / unitBlocks; unit <= (firstBlock + blocks - 1) / unitBlocks; ++unit)
                CountUnit(unit);
    }

    void CountingBlockDevice::CountUnit(uint32_t unit)
    {
        really_assert(unit < maximumUnits);

        if (!written.test(unit))
        {
            written.set(unit);
            ++counts.distinctUnits;
        }

        auto position = static_cast<std::size_t>(std::find(recent.begin(), recent.begin() + recentCount, unit) - recent.begin());
        if (position != recentCount)
            ++counts.unitsByRecency[position];
        else
        {
            ++counts.unitsNotRecent;
            position = std::min(recentCount, recent.size() - 1);
            recentCount = std::min(recentCount + 1, recent.size());
        }

        std::copy_backward(recent.begin(), recent.begin() + position, recent.begin() + position + 1);
        recent.front() = unit;
    }

    infra::Function<void(hal::BlockDevice::Result)> CountingBlockDevice::Start(uint64_t& totalNs, const infra::Function<void(Result)>& onDone)
    {
        really_assert(!Busy());

        this->onDone = onDone;
        meter.Start(totalNs, guard);

        return [this](Result result)
        {
            Completed(result);
        };
    }

    void CountingBlockDevice::Completed(Result result)
    {
        meter.Stop();
        if (result != Result::success)
            ++counts.failedRequests;
        onDone(result);
    }
}
