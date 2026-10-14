#ifndef FS_NOR_TEST_DOUBLES_NOR_IMAGE_FLASH_HPP
#define FS_NOR_TEST_DOUBLES_NOR_IMAGE_FLASH_HPP

#include "fs/nor/test_doubles/NorImage.hpp"
#include "hal/interfaces/Flash.hpp"

namespace fs
{
    class NorImageFlash
        : public hal::Flash
    {
    public:
        explicit NorImageFlash(NorImage& image);

        uint32_t NumberOfSectors() const override;
        uint32_t SizeOfSector(uint32_t sectorIndex) const override;
        uint32_t SectorOfAddress(uint32_t address) const override;
        uint32_t AddressOfSector(uint32_t sectorIndex) const override;
        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

    private:
        NorImage& image;
    };
}

#endif
