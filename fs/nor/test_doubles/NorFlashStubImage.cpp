#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include <algorithm>

namespace fs
{
    NorFlashStubImage::NorFlashStubImage(hal::FlashStub& flash)
        : flash(flash)
    {}

    uint32_t NorFlashStubImage::Sectors() const
    {
        return flash.NumberOfSectors();
    }

    uint32_t NorFlashStubImage::SectorSize() const
    {
        return flash.SizeOfSector(0);
    }

    void NorFlashStubImage::Read(uint32_t address, infra::ByteRange out) const
    {
        for (auto& byte : out)
            byte = Byte(address++);
    }

    void NorFlashStubImage::Program(uint32_t address, infra::ConstByteRange data)
    {
        for (auto byte : data)
            Byte(address++) &= byte;
    }

    void NorFlashStubImage::EraseSector(uint32_t sector)
    {
        std::ranges::fill(flash.sectors[sector], uint8_t{ 0xff });
    }

    uint8_t& NorFlashStubImage::Byte(uint32_t address) const
    {
        return flash.sectors[address / SectorSize()][address % SectorSize()];
    }
}
