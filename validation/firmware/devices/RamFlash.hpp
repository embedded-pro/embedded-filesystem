#ifndef VALIDATION_FIRMWARE_DEVICES_RAM_FLASH_HPP
#define VALIDATION_FIRMWARE_DEVICES_RAM_FLASH_HPP

#include "hal/interfaces/FlashHomogeneous.hpp"
#include "infra/util/ByteRange.hpp"
#include "validation/firmware/devices/RamCompletion.hpp"

namespace validation
{
    class RamFlash
        : public hal::FlashHomogeneous
    {
    public:
        RamFlash(infra::ByteRange storage, uint32_t sizeOfEachSector, RamCompletion completion = RamCompletion::scheduled);

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

    private:
        void Complete(const infra::Function<void()>& onDone) const;

    private:
        infra::ByteRange storage;
        RamCompletion completion;
    };
}

#endif
