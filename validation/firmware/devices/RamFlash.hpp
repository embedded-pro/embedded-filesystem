#ifndef VALIDATION_FIRMWARE_DEVICES_RAM_FLASH_HPP
#define VALIDATION_FIRMWARE_DEVICES_RAM_FLASH_HPP

#include "hal/interfaces/FlashHomogeneous.hpp"
#include "infra/util/ByteRange.hpp"

namespace validation
{
    class RamFlash
        : public hal::FlashHomogeneous
    {
    public:
        RamFlash(infra::ByteRange storage, uint32_t sizeOfEachSector);

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

    private:
        infra::ByteRange storage;
    };
}

#endif
