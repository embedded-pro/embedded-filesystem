#include "metrics/power_cut/PowerCutFlash.hpp"
#include <algorithm>
#include <vector>

namespace metrics
{
    PowerCutFlash::PowerCutFlash(hal::FlashStub& flash)
        : flash(flash)
    {}

    uint32_t PowerCutFlash::NumberOfSectors() const
    {
        return flash.NumberOfSectors();
    }

    uint32_t PowerCutFlash::SizeOfSector(uint32_t sectorIndex) const
    {
        return flash.SizeOfSector(sectorIndex);
    }

    uint32_t PowerCutFlash::SectorOfAddress(uint32_t address) const
    {
        return flash.SectorOfAddress(address);
    }

    uint32_t PowerCutFlash::AddressOfSector(uint32_t sectorIndex) const
    {
        return flash.AddressOfSector(sectorIndex);
    }

    void PowerCutFlash::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        if (poweredDown)
            return;

        if (CutsWithin(1))
        {
            CutProgram(buffer, address);
            return;
        }

        ++writeSteps;
        flash.WriteBuffer(buffer, address, onDone);
    }

    void PowerCutFlash::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        if (!poweredDown)
            flash.ReadBuffer(buffer, address, onDone);
    }

    void PowerCutFlash::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        if (poweredDown)
            return;

        if (CutsWithin(endIndex - beginIndex))
        {
            CutErase(beginIndex, endIndex);
            return;
        }

        writeSteps += endIndex - beginIndex;
        flash.EraseSectors(beginIndex, endIndex, onDone);
    }

    void PowerCutFlash::Arm(const NorCut& newCut)
    {
        cut = newCut;
    }

    void PowerCutFlash::Disarm()
    {
        cut = std::nullopt;
    }

    uint64_t PowerCutFlash::WriteSteps() const
    {
        return writeSteps;
    }

    bool PowerCutFlash::PoweredDown() const
    {
        return poweredDown;
    }

    bool PowerCutFlash::CutsWithin(uint64_t steps) const
    {
        return cut && cut->step >= writeSteps && cut->step < writeSteps + steps;
    }

    void PowerCutFlash::CutProgram(infra::ConstByteRange buffer, uint32_t address)
    {
        if (cut->mode == NorCutMode::partial)
            Program(infra::Head(buffer, std::min<std::size_t>(buffer.size(), cut->partialBytes.value_or(buffer.size() / 2))), address);
        else if (cut->mode == NorCutMode::completed)
            Program(buffer, address);

        writeSteps = cut->step + 1;
        poweredDown = true;
    }

    void PowerCutFlash::CutErase(uint32_t beginIndex, uint32_t endIndex)
    {
        auto cutSector = static_cast<uint32_t>(beginIndex + (cut->step - writeSteps));

        for (auto sector = beginIndex; sector != cutSector; ++sector)
            Erase(sector);

        if (cut->mode != NorCutMode::notStarted)
            Erase(cutSector);

        if (cut->mode == NorCutMode::partial)
        {
            std::vector<uint8_t> zeros(SizeOfSector(cutSector) / 2, 0x00);
            Program(infra::MakeRange(zeros), AddressOfSector(cutSector) + SizeOfSector(cutSector) - zeros.size());
        }

        writeSteps = cut->step + 1;
        poweredDown = true;
    }

    void PowerCutFlash::Program(infra::ConstByteRange buffer, uint32_t address)
    {
        flash.WriteBuffer(buffer, address, [] {});
    }

    void PowerCutFlash::Erase(uint32_t sector)
    {
        flash.EraseSectors(sector, sector + 1, [] {});
    }
}
