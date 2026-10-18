#ifndef VALIDATION_FIRMWARE_BENCH_TEST_DOUBLES_TIMED_BLOCK_DEVICE_HPP
#define VALIDATION_FIRMWARE_BENCH_TEST_DOUBLES_TIMED_BLOCK_DEVICE_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include <chrono>
#include <cstdint>
#include <optional>

namespace validation
{
    // A block device in RAM whose requests complete after a delay of their kind, and which logs them
    class TimedBlockDevice
        : public hal::BlockDevice
    {
    public:
        explicit TimedBlockDevice(infra::ByteRange storage)
            : ram(storage, RamCompletion::immediate)
        {}

        uint32_t BlockSize() const override
        {
            return ram.BlockSize();
        }

        uint32_t NumberOfBlocks() const override
        {
            return ram.NumberOfBlocks();
        }

        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override
        {
            Log("R", firstBlock, buffer.size() / BlockSize());
            ram.ReadBlocks(buffer, firstBlock, [](Result) {});
            Complete(readDelay, onDone);
        }

        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override
        {
            Log("W", firstBlock, buffer.size() / BlockSize());
            ram.WriteBlocks(buffer, firstBlock, [](Result) {});
            Complete(writeDelay, onDone);
        }

        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override
        {
            Log("D", beginBlock, endBlock);
            ram.EraseBlocks(beginBlock, endBlock, [](Result) {});
            Complete(discardDelay, onDone);
        }

        void Flush(const infra::Function<void(Result)>& onDone) override
        {
            Log("F", 0, 0);
            Complete(flushDelay, onDone);
        }

        infra::Duration readDelay{ std::chrono::microseconds(100) };
        infra::Duration writeDelay{ std::chrono::microseconds(200) };
        infra::Duration flushDelay{ std::chrono::milliseconds(1) };
        infra::Duration discardDelay{ std::chrono::milliseconds(2) };
        std::optional<uint32_t> failRequest;
        infra::BoundedString::WithStorage<8192> log;

    private:
        void Log(const char* kind, uint32_t first, std::size_t second)
        {
            infra::StringOutputStream stream{ log };
            stream << kind << first << "," << static_cast<uint32_t>(second) << " ";
        }

        void Complete(infra::Duration delay, const infra::Function<void(Result)>& onDone)
        {
            result = failRequest == requests ? Result::failed : Result::success;
            ++requests;
            pending = onDone;
            timer.Start(delay, [this]
                {
                    pending(result);
                });
        }

        RamBlockDevice ram;
        infra::AutoResetFunction<void(Result)> pending;
        infra::TimerSingleShot timer;
        uint32_t requests{ 0 };
        Result result{ Result::success };
    };
}

#endif
