#ifndef VALIDATION_FIRMWARE_DEVICES_NULL_BLOCK_DEVICE_HPP
#define VALIDATION_FIRMWARE_DEVICES_NULL_BLOCK_DEVICE_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"

namespace validation
{
    class NullBlockDevice
        : public hal::BlockDevice
    {
    public:
        static constexpr uint32_t blockSize = 512;

        explicit NullBlockDevice(uint32_t numberOfBlocks);
        NullBlockDevice(const NullBlockDevice& other) = delete;
        NullBlockDevice& operator=(const NullBlockDevice& other) = delete;
        ~NullBlockDevice() = default;

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

    private:
        void Complete(const infra::Function<void(Result)>& onDone);

    private:
        uint32_t numberOfBlocks;
        infra::AutoResetFunction<void(Result)> pending;
    };
}

#endif
