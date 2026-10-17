#ifndef VALIDATION_FIRMWARE_DEVICES_RAM_BLOCK_DEVICE_HPP
#define VALIDATION_FIRMWARE_DEVICES_RAM_BLOCK_DEVICE_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "validation/firmware/devices/RamCompletion.hpp"

namespace validation
{
    class RamBlockDevice
        : public hal::BlockDevice
    {
    public:
        static constexpr uint32_t blockSize = 512;

        explicit RamBlockDevice(infra::ByteRange storage, RamCompletion completion = RamCompletion::scheduled);
        RamBlockDevice(const RamBlockDevice& other) = delete;
        RamBlockDevice& operator=(const RamBlockDevice& other) = delete;
        ~RamBlockDevice() = default;

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

    private:
        bool IsWithin(uint32_t beginBlock, std::size_t bytes) const;
        void Complete(const infra::Function<void(Result)>& onDone, Result result);

    private:
        infra::ByteRange storage;
        RamCompletion completion;
        infra::AutoResetFunction<void(Result)> pending;
        Result pendingResult{ Result::success };
    };
}

#endif
