#include "validation/firmware/bench/NorBench.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace validation
{
    namespace
    {
        uint32_t SlotBytes(const CountingFlash& flash, uint32_t size)
        {
            return (size + flash.PageSize() - 1) / flash.PageSize() * flash.PageSize();
        }

        uint32_t TotalSize(const CountingFlash& flash)
        {
            return flash.NumberOfSectors() * flash.SizeOfSector(0);
        }

    }

    bool NorBench::ProgramFits(const CountingFlash& flash, uint32_t size, uint32_t repetitions)
    {
        if (!Repeatable(repetitions) || size == 0 || size > std::min(maximumProgramBytes, flash.SizeOfSector(0)) || SlotBytes(flash, size) > flash.SizeOfSector(0))
            return false;

        auto slots = flash.SizeOfSector(0) / SlotBytes(flash, size);
        return (repetitions + slots - 1) / slots <= flash.NumberOfSectors();
    }

    bool NorBench::EraseFits(const CountingFlash& flash, uint32_t repetitions)
    {
        return Repeatable(repetitions) && repetitions <= flash.NumberOfSectors();
    }

    bool NorBench::ReadFits(const CountingFlash& flash, uint32_t size, uint32_t repetitions)
    {
        return Repeatable(repetitions) && size != 0 && size <= std::min(maximumReadBytes, TotalSize(flash));
    }

    NorBench::NorBench(ResultLines& results, uint32_t clockHz)
        : Benchmark(results, clockHz)
    {
        for (std::size_t position = 0; position != data.size(); ++position)
            data[position] = static_cast<uint8_t>(position * 7 + 1);
    }

    void NorBench::Program(CountingFlash& flash, infra::BoundedConstString device, uint32_t size, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(ProgramFits(flash, size, repetitions));

        Start(flash, device, "program", size, repetitions, onDone);
        slotBytes = SlotBytes(flash, size);
        slotsPerSector = flash.SizeOfSector(0) / slotBytes;
        EraseForProgram();
    }

    void NorBench::Erase(CountingFlash& flash, infra::BoundedConstString device, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(EraseFits(flash, repetitions));

        Start(flash, device, "erase", flash.SizeOfSector(0), repetitions, onDone);
        EraseBeforeFill();
    }

    void NorBench::Read(CountingFlash& flash, infra::BoundedConstString device, uint32_t size, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(ReadFits(flash, size, repetitions));

        Start(flash, device, "read", size, repetitions, onDone);
        ReadNext();
    }

    // A tag at each power of two lands on a sector of its own only if every address line works, and reads back only if
    // every data line does; each sector is erased before its tag, so that a sector a missing line aliases is never programmed twice
    void NorBench::Check(CountingFlash& flash, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(flash.PageSize() <= maximumProgramBytes && flash.PageSize() >= sizeof(uint32_t));

        Start(flash, "", "check", flash.PageSize(), 1, onDone);
        checkSectors = 1;
        while ((1u << (checkSectors - 1)) < flash.NumberOfSectors())
            ++checkSectors;
        mismatch = false;
        EraseForCheck();
    }

    void NorBench::Start(CountingFlash& flash, infra::BoundedConstString device, const char* operation, uint32_t size, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(Repeatable(repetitions));

        Begin(flash.Meter(), { device, operation, size }, onDone);
        this->flash = &flash;
        this->size = size;
        this->repetitions = repetitions;
        count = 0;
        sector = 0;
        slot = 0;
    }

    infra::Function<void()> NorBench::Then(Step next)
    {
        resumeWith = next;
        return [this]
        {
            Resume(false);
        };
    }

    infra::Function<void()> NorBench::Measured(Step next)
    {
        resumeWith = next;
        return [this]
        {
            Resume(true);
        };
    }

    void NorBench::Resume(bool measured)
    {
        if (Expired())
            Stop();
        else
        {
            if (measured)
                Measure();
            (this->*resumeWith)();
        }
    }

    void NorBench::Completed()
    {
        Report();
    }

    void NorBench::EraseForProgram()
    {
        flash->EraseSectors(sector, sector + 1, Then(&NorBench::ProgramNext));
    }

    void NorBench::ProgramNext()
    {
        if (count == repetitions)
            EraseProgrammed();
        else if (slot == slotsPerSector)
        {
            ++sector;
            slot = 0;
            EraseForProgram();
        }
        else
        {
            auto address = flash->AddressOfSector(sector) + slot * slotBytes;
            ++slot;
            ++count;
            flash->WriteBuffer(infra::Head(infra::MakeRange(data), size), address, Measured(&NorBench::ProgramNext));
        }
    }

    void NorBench::EraseProgrammed()
    {
        flash->EraseSectors(0, sector + 1, Then(&NorBench::Completed));
    }

    void NorBench::EraseBeforeFill()
    {
        if (sector == repetitions)
            Report();
        else
        {
            fillOffset = 0;
            flash->EraseSectors(sector, sector + 1, Then(&NorBench::FillNext));
        }
    }

    void NorBench::FillNext()
    {
        if (fillOffset == size)
            flash->EraseSectors(sector, sector + 1, Measured(&NorBench::NextSector));
        else
        {
            auto chunk = std::min<uint32_t>(maximumProgramBytes, size - fillOffset);
            auto address = flash->AddressOfSector(sector) + fillOffset;
            fillOffset += chunk;
            flash->WriteBuffer(infra::Head(infra::MakeRange(data), chunk), address, Then(&NorBench::FillNext));
        }
    }

    void NorBench::NextSector()
    {
        ++sector;
        EraseBeforeFill();
    }

    void NorBench::ReadNext()
    {
        if (count == repetitions)
            Report();
        else
        {
            auto span = TotalSize(*flash) / size * size;
            auto address = count * size % span;
            ++count;
            flash->ReadBuffer(infra::Head(infra::MakeRange(readBuffer), size), address, Measured(&NorBench::ReadNext));
        }
    }

    uint32_t NorBench::CheckSector() const
    {
        return count == 0 ? 0 : 1u << (count - 1);
    }

    infra::ConstByteRange NorBench::Tag(uint32_t sector)
    {
        auto tag = infra::Head(infra::MakeRange(data), size);
        for (std::size_t position = 0; position != tag.size(); ++position)
            tag[position] = static_cast<uint8_t>(position < sizeof(sector) ? sector >> (8 * position) : position * 7 + sector + 1);
        return tag;
    }

    void NorBench::EraseForCheck()
    {
        if (count == checkSectors)
        {
            count = 0;
            ReadTag();
        }
        else
            flash->EraseSectors(CheckSector(), CheckSector() + 1, Then(&NorBench::ProgramTag));
    }

    void NorBench::ProgramTag()
    {
        auto sector = CheckSector();
        ++count;
        flash->WriteBuffer(Tag(sector), flash->AddressOfSector(sector), Then(&NorBench::EraseForCheck));
    }

    void NorBench::ReadTag()
    {
        if (count == checkSectors)
        {
            count = 0;
            EraseChecked();
        }
        else
            flash->ReadBuffer(infra::Head(infra::MakeRange(readBuffer), size), flash->AddressOfSector(CheckSector()), Then(&NorBench::CompareTag));
    }

    void NorBench::CompareTag()
    {
        auto expected = Tag(CheckSector());
        mismatch = mismatch || !std::equal(expected.begin(), expected.end(), readBuffer.begin());
        ++count;
        ReadTag();
    }

    void NorBench::EraseChecked()
    {
        if (count == checkSectors)
            Finish(mismatch ? BenchOutcome::failed : BenchOutcome::done);
        else
        {
            auto sector = CheckSector();
            ++count;
            flash->EraseSectors(sector, sector + 1, Then(&NorBench::EraseChecked));
        }
    }
}
