#ifndef VALIDATION_FIRMWARE_BENCH_TEST_DOUBLES_TIMED_FLASH_HPP
#define VALIDATION_FIRMWARE_BENCH_TEST_DOUBLES_TIMED_FLASH_HPP

#include "hal/interfaces/Flash.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedDeque.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/devices/RamFlash.hpp"
#include <chrono>
#include <cstdint>

namespace validation
{
    // NOR flash in RAM whose requests complete after a delay of their kind, and which logs them
    class TimedFlash
        : public hal::Flash
    {
    public:
        TimedFlash(infra::ByteRange storage, uint32_t sectorSize)
            : ram(storage, sectorSize, RamCompletion::immediate)
        {}

        uint32_t NumberOfSectors() const override
        {
            return ram.NumberOfSectors();
        }

        uint32_t SizeOfSector(uint32_t sectorIndex) const override
        {
            return ram.SizeOfSector(sectorIndex);
        }

        uint32_t SectorOfAddress(uint32_t address) const override
        {
            return ram.SectorOfAddress(address);
        }

        uint32_t AddressOfSector(uint32_t sectorIndex) const override
        {
            return ram.AddressOfSector(sectorIndex);
        }

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override
        {
            Log("P", address, buffer.size());
            ram.WriteBuffer(buffer, address, [] {});
            Complete(programDelay, onDone);
        }

        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override
        {
            Log("R", address, buffer.size());
            ram.ReadBuffer(buffer, address, [] {});
            auto delay = readDelay;
            if (!readDelays.empty())
            {
                delay = readDelays.front();
                readDelays.pop_front();
            }
            Complete(delay, onDone);
        }

        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override
        {
            Log("E", beginIndex, endIndex);
            ram.EraseSectors(beginIndex, endIndex, [] {});
            Complete(eraseDelay, onDone);
        }

        infra::Duration programDelay{ std::chrono::microseconds(100) };
        infra::Duration eraseDelay{ std::chrono::milliseconds(2) };
        infra::Duration readDelay{ std::chrono::microseconds(10) };
        infra::BoundedDeque<infra::Duration>::WithMaxSize<8> readDelays;
        infra::BoundedString::WithStorage<4096> log;

    private:
        void Log(const char* kind, uint32_t first, std::size_t second)
        {
            infra::StringOutputStream stream{ log };
            stream << kind << first << "," << static_cast<uint32_t>(second) << " ";
        }

        void Complete(infra::Duration delay, const infra::Function<void()>& onDone)
        {
            pending = onDone;
            timer.Start(delay, [this]
                {
                    pending();
                });
        }

        RamFlash ram;
        infra::AutoResetFunction<void()> pending;
        infra::TimerSingleShot timer;
    };
}

#endif
