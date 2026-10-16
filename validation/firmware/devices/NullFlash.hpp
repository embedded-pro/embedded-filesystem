#ifndef VALIDATION_FIRMWARE_DEVICES_NULL_FLASH_HPP
#define VALIDATION_FIRMWARE_DEVICES_NULL_FLASH_HPP

#include "hal/interfaces/FlashHomogeneous.hpp"

namespace validation
{
    class NullFlash
        : public hal::FlashHomogeneous
    {
    public:
        NullFlash(uint32_t numberOfSectors, uint32_t sizeOfEachSector);

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;
    };
}

#endif
