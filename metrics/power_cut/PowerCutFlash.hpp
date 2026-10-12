#ifndef METRICS_POWER_CUT_POWER_CUT_FLASH_HPP
#define METRICS_POWER_CUT_POWER_CUT_FLASH_HPP

#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include <cstdint>
#include <optional>

namespace metrics
{
    enum class NorCutMode : uint8_t
    {
        notStarted,
        partial,
        completed
    };

    struct NorCut
    {
        uint64_t step;
        NorCutMode mode;
        std::optional<uint32_t> partialBytes;
    };

    class PowerCutFlash
        : public hal::Flash
    {
    public:
        explicit PowerCutFlash(hal::FlashStub& flash);

        uint32_t NumberOfSectors() const override;
        uint32_t SizeOfSector(uint32_t sectorIndex) const override;
        uint32_t SectorOfAddress(uint32_t address) const override;
        uint32_t AddressOfSector(uint32_t sectorIndex) const override;

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

        void Arm(const NorCut& cut);
        void Disarm();
        uint64_t WriteSteps() const;
        bool PoweredDown() const;

    private:
        bool CutsWithin(uint64_t steps) const;
        void CutProgram(infra::ConstByteRange buffer, uint32_t address);
        void CutErase(uint32_t beginIndex, uint32_t endIndex);
        void Program(infra::ConstByteRange buffer, uint32_t address);
        void Erase(uint32_t sector);

    private:
        hal::FlashStub& flash;
        std::optional<NorCut> cut;
        uint64_t writeSteps{};
        bool poweredDown{ false };
    };
}

#endif
