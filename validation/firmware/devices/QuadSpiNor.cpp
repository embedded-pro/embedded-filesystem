#include "validation/firmware/devices/QuadSpiNor.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace validation
{
    namespace
    {
        constexpr uint8_t commandReadJedecId = 0x9f;
        constexpr uint8_t commandWriteEnable = 0x06;
        constexpr uint8_t commandReadVolatileConfiguration = 0x85;
        constexpr uint8_t commandWriteVolatileConfiguration = 0x81;
        constexpr uint8_t manufacturerMicron = 0x20;
        constexpr uint8_t dummyCyclesShift = 4;
        constexpr uint8_t dummyCyclesMask = 0xf0;
        constexpr uint8_t maximumDummyCycles = 14;
        constexpr uint8_t defaultDummyCycles = 15;

        hal::QuadSpi::Header Command(uint8_t command)
        {
            return hal::QuadSpi::Header{ command, {}, {}, 0 };
        }
    }

    QuadSpiNor::QuadSpiNor(hal::QuadSpi& spi, uint32_t regionSectors)
        : spi(spi)
        , regionSectors(regionSectors)
    {}

    void QuadSpiNor::Start(const services::FlashGeometryQuad& geometry, const infra::Function<void(bool present)>& onDone)
    {
        this->geometry = &geometry;
        this->onDone = onDone;
        region.reset();
        flash.reset();
        spi.ReceiveData(Command(commandReadJedecId), infra::MakeRange(identification), hal::QuadSpi::Lines::SingleSpeed(), [this]
            {
                Identified();
            });
    }

    bool QuadSpiNor::Present() const
    {
        return region.has_value();
    }

    uint32_t QuadSpiNor::JedecId() const
    {
        return static_cast<uint32_t>(identification[0]) << 16 | static_cast<uint32_t>(identification[1]) << 8 | identification[2];
    }

    uint32_t QuadSpiNor::NumberOfSectors() const
    {
        return Region().NumberOfSectors();
    }

    uint32_t QuadSpiNor::SizeOfSector(uint32_t sectorIndex) const
    {
        return Region().SizeOfSector(sectorIndex);
    }

    uint32_t QuadSpiNor::SectorOfAddress(uint32_t address) const
    {
        return Region().SectorOfAddress(address);
    }

    uint32_t QuadSpiNor::AddressOfSector(uint32_t sectorIndex) const
    {
        return Region().AddressOfSector(sectorIndex);
    }

    void QuadSpiNor::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        Region().WriteBuffer(buffer, address, onDone);
    }

    void QuadSpiNor::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        Region().ReadBuffer(buffer, address, onDone);
    }

    void QuadSpiNor::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        Region().EraseSectors(beginIndex, endIndex, onDone);
    }

    // A Micron flash takes its read dummy cycles from its volatile configuration, which a debugger or a former image may have changed
    void QuadSpiNor::Identified()
    {
        if (!Answers() || geometry->NrOfSubSectors() < regionSectors)
            Finish(false);
        else if (identification[0] == manufacturerMicron && geometry->ReadDummyCycles() != 0 && geometry->ReadDummyCycles() <= maximumDummyCycles)
            spi.ReceiveData(Command(commandReadVolatileConfiguration), infra::MakeRange(volatileConfiguration), hal::QuadSpi::Lines::SingleSpeed(), [this]
                {
                    ConfigurationRead();
                });
        else
            Finish(true);
    }

    void QuadSpiNor::ConfigurationRead()
    {
        auto dummyCycles = ConfiguredDummyCycles();
        if (dummyCycles == 0 || dummyCycles == defaultDummyCycles || dummyCycles == geometry->ReadDummyCycles())
            Finish(true);
        else
            spi.SendData(Command(commandWriteEnable), infra::ConstByteRange(), hal::QuadSpi::Lines::SingleSpeed(), [this]
                {
                    volatileConfiguration[0] = static_cast<uint8_t>((volatileConfiguration[0] & ~dummyCyclesMask) | (geometry->ReadDummyCycles() << dummyCyclesShift));
                    spi.SendData(Command(commandWriteVolatileConfiguration), infra::MakeConstRange(volatileConfiguration), hal::QuadSpi::Lines::SingleSpeed(), [this]
                        {
                            ConfigurationWritten();
                        });
                });
    }

    void QuadSpiNor::ConfigurationWritten()
    {
        spi.ReceiveData(Command(commandReadVolatileConfiguration), infra::MakeRange(volatileConfiguration), hal::QuadSpi::Lines::SingleSpeed(), [this]
            {
                ConfigurationVerified();
            });
    }

    void QuadSpiNor::ConfigurationVerified()
    {
        Finish(ConfiguredDummyCycles() == geometry->ReadDummyCycles());
    }

    void QuadSpiNor::Finish(bool present)
    {
        if (present)
        {
            flash.emplace(spi, *geometry, services::FlashQuadSpiGeneric::Protocol::extendedSpi);
            region.emplace(*flash, 0, regionSectors);
        }

        onDone(present);
    }

    bool QuadSpiNor::Answers() const
    {
        return identification[0] != 0x00 && identification[0] != 0xff;
    }

    uint8_t QuadSpiNor::ConfiguredDummyCycles() const
    {
        return static_cast<uint8_t>((volatileConfiguration[0] & dummyCyclesMask) >> dummyCyclesShift);
    }

    hal::Flash& QuadSpiNor::Region()
    {
        really_assert(region.has_value());
        return *region;
    }

    const hal::Flash& QuadSpiNor::Region() const
    {
        really_assert(region.has_value());
        return *region;
    }
}
