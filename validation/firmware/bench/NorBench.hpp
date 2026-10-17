#ifndef VALIDATION_FIRMWARE_BENCH_NOR_BENCH_HPP
#define VALIDATION_FIRMWARE_BENCH_NOR_BENCH_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/timing/Samples.hpp"
#include <array>
#include <cstdint>

namespace validation
{
    enum class BenchOutcome : uint8_t
    {
        done,
        failed,
        timeout
    };

    class NorBench
    {
    public:
        static constexpr uint32_t maximumRepetitions = 1024;
        static constexpr uint32_t maximumProgramBytes = 4096;
        static constexpr uint32_t maximumReadBytes = 65536;

        static bool ProgramFits(const CountingFlash& flash, uint32_t size, uint32_t repetitions);
        static bool EraseFits(const CountingFlash& flash, uint32_t repetitions);
        static bool ReadFits(const CountingFlash& flash, uint32_t size, uint32_t repetitions);

        NorBench(ResultLines& results, uint32_t clockHz);
        NorBench(const NorBench& other) = delete;
        NorBench& operator=(const NorBench& other) = delete;
        ~NorBench() = default;

        void Program(CountingFlash& flash, infra::BoundedConstString device, uint32_t size, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        void Erase(CountingFlash& flash, infra::BoundedConstString device, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        void Read(CountingFlash& flash, infra::BoundedConstString device, uint32_t size, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        void Check(CountingFlash& flash, const infra::Function<void(BenchOutcome outcome)>& onDone);
        bool Running() const;

    private:
        using Step = void (NorBench::*)();

        void Start(CountingFlash& flash, infra::BoundedConstString device, const char* operation, uint32_t size, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        infra::Function<void()> Then(Step next);
        infra::Function<void()> Measured(Step next);
        void TimedOut();
        void Stop();
        void Finish(BenchOutcome outcome);
        void Report();

        void EraseForProgram();
        void ProgramNext();
        void EraseProgrammed();
        void EraseBeforeFill();
        void FillNext();
        void NextSector();
        void ReadNext();

        uint32_t CheckSector() const;
        infra::ConstByteRange Tag(uint32_t sector);
        void EraseForCheck();
        void ProgramTag();
        void ReadTag();
        void CompareTag();
        void EraseChecked();

    private:
        ResultLines& results;
        uint32_t clockHz;

        CountingFlash* flash{ nullptr };
        infra::BoundedConstString device;
        const char* operation{ nullptr };
        uint32_t size{ 0 };
        uint32_t repetitions{ 0 };
        infra::AutoResetFunction<void(BenchOutcome outcome)> onDone;
        bool running{ false };
        bool timedOut{ false };
        Step resumeWith{ nullptr };

        uint32_t count{ 0 };
        uint32_t sector{ 0 };
        uint32_t slot{ 0 };
        uint32_t slotBytes{ 0 };
        uint32_t slotsPerSector{ 0 };
        uint32_t fillOffset{ 0 };
        uint32_t checkSectors{ 0 };
        bool mismatch{ false };
        Samples::WithCapacity<maximumRepetitions> samples;
        std::array<uint8_t, maximumProgramBytes> data{};
        std::array<uint8_t, maximumReadBytes> readBuffer{};
    };
}

#endif
