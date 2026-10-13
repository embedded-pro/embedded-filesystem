#include "metrics/device/MeteredBlockDevice.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace metrics
{
    MeteredBlockDevice::MeteredBlockDevice(hal::BlockDevice& device, const BlockDeviceModel& model, TimingCase timingCase)
        : device(device)
        , model(model)
        , timingCase(timingCase)
        , writesPerAllocationUnit((device.NumberOfBlocks() + model.blocksPerAllocationUnit - 1) / model.blocksPerAllocationUnit, 0)
    {}

    uint32_t MeteredBlockDevice::BlockSize() const
    {
        return device.BlockSize();
    }

    uint32_t MeteredBlockDevice::NumberOfBlocks() const
    {
        return device.NumberOfBlocks();
    }

    void MeteredBlockDevice::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        Begin({ Kind::read, firstBlock, static_cast<uint32_t>(buffer.size() / BlockSize()) }, onDone);
        ++counters.readRequests;
        device.ReadBlocks(buffer, firstBlock, [this](Result result)
            {
                WrappedDone(result);
            });
    }

    void MeteredBlockDevice::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        Begin({ Kind::write, firstBlock, static_cast<uint32_t>(buffer.size() / BlockSize()) }, onDone);
        ++counters.writeRequests;
        device.WriteBlocks(buffer, firstBlock, [this](Result result)
            {
                WrappedDone(result);
            });
    }

    void MeteredBlockDevice::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        Begin({ Kind::discard, beginBlock, endBlock > beginBlock ? endBlock - beginBlock : 0 }, onDone);
        ++counters.discardRequests;
        device.EraseBlocks(beginBlock, endBlock, [this](Result result)
            {
                WrappedDone(result);
            });
    }

    void MeteredBlockDevice::Flush(const infra::Function<void(Result)>& onDone)
    {
        Begin({ Kind::flush, 0, 0 }, onDone);
        ++counters.flushRequests;
        device.Flush([this](Result result)
            {
                WrappedDone(result);
            });
    }

    const BlockDeviceCounters& MeteredBlockDevice::Counters() const
    {
        return counters;
    }

    const std::map<uint32_t, uint64_t>& MeteredBlockDevice::BlocksPerWriteHistogram() const
    {
        return blocksPerWrite;
    }

    const std::vector<uint64_t>& MeteredBlockDevice::WritesPerAllocationUnit() const
    {
        return writesPerAllocationUnit;
    }

    const BlockDeviceModel& MeteredBlockDevice::Model() const
    {
        return model;
    }

    bool MeteredBlockDevice::Busy() const
    {
        return static_cast<bool>(onDone);
    }

    void MeteredBlockDevice::Begin(const Request& newRequest, const infra::Function<void(Result)>& newOnDone)
    {
        really_assert(!Busy());
        request = newRequest;
        onDone = newOnDone;
    }

    void MeteredBlockDevice::WrappedDone(Result wrappedResult)
    {
        result = wrappedResult;
        completionTimer.Start(Account(result), [this]
            {
                infra::EventDispatcher::Instance().Schedule([this]
                    {
                        onDone(result);
                    });
            });
    }

    infra::Duration MeteredBlockDevice::Account(Result accountedResult)
    {
        if (accountedResult == Result::success)
            return AccountSuccess();

        ++counters.failedRequests;
        return Charge(CommandTime(model, timingCase));
    }

    infra::Duration MeteredBlockDevice::AccountSuccess()
    {
        switch (request.kind)
        {
            case Kind::read:
                counters.blocksRead += request.blocks;
                return Charge(ReadTime(model, timingCase, request.blocks));
            case Kind::write:
                return AccountWrite();
            case Kind::discard:
                counters.blocksDiscarded += request.blocks;
                return Charge(CommandTime(model, timingCase));
            default:
                return Charge(CommandTime(model, timingCase));
        }
    }

    infra::Duration MeteredBlockDevice::Charge(infra::Duration deviceTime)
    {
        TimeCounter() += deviceTime;
        return deviceTime;
    }

    infra::Duration& MeteredBlockDevice::TimeCounter()
    {
        switch (request.kind)
        {
            case Kind::read:
                return counters.readTime;
            case Kind::write:
                return counters.writeTime;
            case Kind::discard:
                return counters.discardTime;
            default:
                return counters.flushTime;
        }
    }

    infra::Duration MeteredBlockDevice::AccountWrite()
    {
        auto continues = request.firstBlock % model.blocksPerAllocationUnit == 0 || nextSequentialBlock == request.firstBlock;
        counters.alignedBlocks += continues || request.blocks == 0 ? request.blocks : request.blocks - 1;

        nextSequentialBlock = request.firstBlock + request.blocks;
        counters.blocksWritten += request.blocks;
        ++blocksPerWrite[request.blocks];

        auto openings = TrackAllocationUnits(request.firstBlock, request.blocks);
        counters.allocationUnitOpenings += openings;

        return Charge(WriteTime(model, timingCase, request.blocks, openings));
    }

    uint32_t MeteredBlockDevice::TrackAllocationUnits(uint32_t firstBlock, uint32_t blocks)
    {
        if (blocks == 0)
            return 0;

        uint32_t openings = 0;
        for (auto unit = firstBlock / model.blocksPerAllocationUnit; unit <= (firstBlock + blocks - 1) / model.blocksPerAllocationUnit; ++unit)
        {
            ++writesPerAllocationUnit[unit];
            if (OpensAllocationUnit(unit))
                ++openings;
        }

        return openings;
    }

    bool MeteredBlockDevice::OpensAllocationUnit(uint32_t unit)
    {
        auto recent = std::find(recentAllocationUnits.begin(), recentAllocationUnits.end(), unit);
        if (recent != recentAllocationUnits.end())
        {
            recentAllocationUnits.erase(recent);
            recentAllocationUnits.push_front(unit);
            return false;
        }

        recentAllocationUnits.push_front(unit);
        if (recentAllocationUnits.size() <= model.recentAllocationUnits)
            return false;

        recentAllocationUnits.pop_back();
        return true;
    }
}
