#ifndef METRICS_DEVICE_METERED_BLOCK_DEVICE_HPP
#define METRICS_DEVICE_METERED_BLOCK_DEVICE_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "metrics/device/BlockDeviceStatistics.hpp"
#include "metrics/device/DeviceModels.hpp"
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace metrics
{
    class MeteredBlockDevice
        : public hal::BlockDevice
    {
    public:
        MeteredBlockDevice(hal::BlockDevice& device, const BlockDeviceModel& model, TimingCase timingCase = TimingCase::typical);

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

        const BlockDeviceCounters& Counters() const;
        const std::map<uint32_t, uint64_t>& BlocksPerWriteHistogram() const;
        const std::vector<uint64_t>& WritesPerAllocationUnit() const;
        const BlockDeviceModel& Model() const;
        bool Busy() const;

    private:
        enum class Kind : uint8_t
        {
            read,
            write,
            discard,
            flush
        };

        struct Request
        {
            Kind kind;
            uint32_t firstBlock;
            uint32_t blocks;
        };

        void Begin(const Request& request, const infra::Function<void(Result)>& onDone);
        void WrappedDone(Result result);
        infra::Duration Account(Result result);
        infra::Duration AccountSuccess();
        infra::Duration AccountWrite();
        infra::Duration Charge(infra::Duration deviceTime);
        infra::Duration& TimeCounter();
        uint32_t TrackAllocationUnits(uint32_t firstBlock, uint32_t blocks);
        bool OpensAllocationUnit(uint32_t unit);

    private:
        hal::BlockDevice& device;
        const BlockDeviceModel& model;
        TimingCase timingCase;
        BlockDeviceCounters counters;
        std::map<uint32_t, uint64_t> blocksPerWrite;
        std::vector<uint64_t> writesPerAllocationUnit;
        std::deque<uint32_t> recentAllocationUnits;
        std::optional<uint32_t> nextSequentialBlock;
        Request request{};
        infra::AutoResetFunction<void(Result)> onDone;
        Result result{ Result::success };
        infra::TimerSingleShot completionTimer;
    };
}

#endif
