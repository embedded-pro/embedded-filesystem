#include "validation/firmware/bench/NorBench.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "validation/firmware/timing/Nanoseconds.hpp"
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
        if (size == 0 || size > std::min(maximumProgramBytes, flash.SizeOfSector(0)))
            return false;

        auto slotsPerSector = flash.SizeOfSector(0) / SlotBytes(flash, size);
        return (repetitions + slotsPerSector - 1) / slotsPerSector <= flash.NumberOfSectors();
    }

    bool NorBench::EraseFits(const CountingFlash& flash, uint32_t repetitions)
    {
        return repetitions <= flash.NumberOfSectors();
    }

    bool NorBench::ReadFits(const CountingFlash& flash, uint32_t size)
    {
        return size != 0 && size <= std::min(maximumReadBytes, TotalSize(flash));
    }

    NorBench::NorBench(ResultLines& results, uint32_t clockHz)
        : results(results)
        , clockHz(clockHz)
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
        really_assert(ReadFits(flash, size));

        Start(flash, device, "read", size, repetitions, onDone);
        ReadNext();
    }

    bool NorBench::Running() const
    {
        return running;
    }

    void NorBench::Start(CountingFlash& flash, infra::BoundedConstString device, const char* operation, uint32_t size, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(!running && repetitions >= 1 && repetitions <= maximumRepetitions);

        this->flash = &flash;
        this->device = device;
        this->operation = operation;
        this->size = size;
        this->repetitions = repetitions;
        this->onDone = onDone;
        running = true;
        timedOut = false;
        count = 0;
        sector = 0;
        slot = 0;
        samples.Clear();
        flash.OnTimeout([this]
            {
                TimedOut();
            });
    }

    infra::Function<void()> NorBench::Then(Step next)
    {
        resumeWith = next;
        return [this]
        {
            if (timedOut)
                running = false;
            else
                (this->*resumeWith)();
        };
    }

    infra::Function<void()> NorBench::Measured(Step next)
    {
        resumeWith = next;
        return [this]
        {
            if (timedOut)
                running = false;
            else
            {
                samples.Add(flash->LastCycles());
                (this->*resumeWith)();
            }
        };
    }

    void NorBench::TimedOut()
    {
        timedOut = true;
        onDone(BenchOutcome::timeout);
    }

    void NorBench::Report()
    {
        results.Record("bench")
            .Add("device", device)
            .Add("op", operation)
            .Add("size", size)
            .Add("n", repetitions)
            .Add("minNs", Nanoseconds(samples.Minimum(), clockHz))
            .Add("medianNs", Nanoseconds(samples.Median(), clockHz))
            .Add("p99Ns", Nanoseconds(samples.Percentile(99), clockHz))
            .Add("maxNs", Nanoseconds(samples.Maximum(), clockHz))
            .Add("meanNs", Nanoseconds(samples.Mean(), clockHz));

        running = false;
        onDone(BenchOutcome::done);
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
        flash->EraseSectors(0, sector + 1, Then(&NorBench::Report));
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
}
