#ifndef VALIDATION_FIRMWARE_DEVICES_COUNTING_BLOCK_DEVICE_HPP
#define VALIDATION_FIRMWARE_DEVICES_COUNTING_BLOCK_DEVICE_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/devices/CountedDevice.hpp"
#include "validation/firmware/devices/RequestMeter.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace validation
{
    // Counts the requests of a block device as metrics::MeteredBlockDevice does on the host, and measures them: the
    // allocation units that writes span are counted by their place among the four most recently written ones, so that the
    // openings of a model that keeps fewer of them open follow from the counts
    class CountingBlockDevice
        : public hal::BlockDevice
        , public CountedDevice
    {
    public:
        static constexpr std::size_t maximumUnits = 4096;

        CountingBlockDevice(hal::BlockDevice& device, uint32_t unitBlocks, const CycleCounter& cycles, uint32_t clockHz, infra::Duration guard);
        CountingBlockDevice(const CountingBlockDevice& other) = delete;
        CountingBlockDevice& operator=(const CountingBlockDevice& other) = delete;
        ~CountingBlockDevice() = default;

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

        DeviceCounts Snapshot() const override;
        void StartPhase() override;
        void OnTimeout(const infra::Function<void()>& onTimeout) override;
        RequestMeter& Meter();

        uint32_t UnitBlocks() const;
        const BlockCounts& Counts() const;
        uint32_t LastCycles() const;
        bool Busy() const;
        bool InUse() const override;

    private:
        uint32_t Blocks(infra::ConstByteRange buffer) const;
        void CountWrite(uint32_t firstBlock, uint32_t blocks);
        void CountUnit(uint32_t unit);
        infra::Function<void(Result)> Start(uint64_t& totalNs, const infra::Function<void(Result)>& onDone);
        void Completed(Result result);

    private:
        hal::BlockDevice& device;
        uint32_t unitBlocks;
        infra::Duration guard;
        BlockCounts counts;
        RequestMeter meter;
        infra::AutoResetFunction<void(Result)> onDone;
        uint32_t nextSequentialBlock{ 0 };
        std::array<uint32_t, BlockCounts::recentUnits> recent{};
        std::size_t recentCount{ 0 };
        std::bitset<maximumUnits> written;
    };
}

#endif
