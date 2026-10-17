#ifndef VALIDATION_FIRMWARE_DEVICES_QUAD_SPI_NOR_HPP
#define VALIDATION_FIRMWARE_DEVICES_QUAD_SPI_NOR_HPP

#include "hal/interfaces/Flash.hpp"
#include "hal/interfaces/QuadSpi.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/flash/FlashGeometryQuad.hpp"
#include "services/flash/FlashQuadSpiGeneric.hpp"
#include "services/flash/FlashRegion.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    // The first sectors of a QSPI NOR flash, set up as the demos of hal-st set it up once its SFDP geometry is read
    class QuadSpiNor
        : public hal::Flash
    {
    public:
        QuadSpiNor(hal::QuadSpi& spi, uint32_t regionSectors);
        QuadSpiNor(const QuadSpiNor& other) = delete;
        QuadSpiNor& operator=(const QuadSpiNor& other) = delete;
        ~QuadSpiNor() = default;

        void Start(const services::FlashGeometryQuad& geometry, const infra::Function<void(bool present)>& onDone);
        bool Present() const;
        uint32_t JedecId() const;

        uint32_t NumberOfSectors() const override;
        uint32_t SizeOfSector(uint32_t sectorIndex) const override;
        uint32_t SectorOfAddress(uint32_t address) const override;
        uint32_t AddressOfSector(uint32_t sectorIndex) const override;

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

    private:
        void Identified();
        void ConfigurationRead();
        void ConfigurationWritten();
        void ConfigurationVerified();
        void Finish(bool present);
        bool Answers() const;
        uint8_t ConfiguredDummyCycles() const;
        hal::Flash& Region() const;

    private:
        hal::QuadSpi& spi;
        uint32_t regionSectors;
        const services::FlashGeometryQuad* geometry{ nullptr };
        infra::AutoResetFunction<void(bool present)> onDone;
        std::array<uint8_t, 3> identification{};
        std::array<uint8_t, 1> volatileConfiguration{};
        std::optional<services::FlashQuadSpiGeneric> flash;
        std::optional<services::FlashRegion> region;
    };
}

#endif
