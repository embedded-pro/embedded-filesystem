#ifndef FS_NOR_TEST_DOUBLES_NOR_FLASH_STUB_IMAGE_HPP
#define FS_NOR_TEST_DOUBLES_NOR_FLASH_STUB_IMAGE_HPP

#include "fs/nor/test_doubles/NorImage.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"

namespace fs
{
    class NorFlashStubImage
        : public NorImage
    {
    public:
        explicit NorFlashStubImage(hal::FlashStub& flash);

        uint32_t Sectors() const override;
        uint32_t SectorSize() const override;
        void Read(uint32_t address, infra::ByteRange out) const override;
        void Program(uint32_t address, infra::ConstByteRange data) override;
        void EraseSector(uint32_t sector) override;

    private:
        uint8_t& Byte(uint32_t address) const;

    private:
        hal::FlashStub& flash;
    };
}

#endif
