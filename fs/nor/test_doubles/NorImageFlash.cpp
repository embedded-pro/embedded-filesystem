#include "fs/nor/test_doubles/NorImageFlash.hpp"

namespace fs
{
    NorImageFlash::NorImageFlash(NorImage& image)
        : image(image)
    {}

    uint32_t NorImageFlash::NumberOfSectors() const
    {
        return image.Sectors();
    }

    uint32_t NorImageFlash::SizeOfSector(uint32_t) const
    {
        return image.SectorSize();
    }

    uint32_t NorImageFlash::SectorOfAddress(uint32_t address) const
    {
        return address / image.SectorSize();
    }

    uint32_t NorImageFlash::AddressOfSector(uint32_t sectorIndex) const
    {
        return sectorIndex * image.SectorSize();
    }

    void NorImageFlash::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        image.Program(address, buffer);
        onDone();
    }

    void NorImageFlash::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        image.Read(address, buffer);
        onDone();
    }

    void NorImageFlash::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        for (auto sector = beginIndex; sector != endIndex; ++sector)
            image.EraseSector(sector);

        onDone();
    }
}
