#ifndef VALIDATION_FIRMWARE_BENCH_BLOCK_BENCH_HPP
#define VALIDATION_FIRMWARE_BENCH_BLOCK_BENCH_HPP

#include "fs/common/TransferAlignment.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/bench/Benchmark.hpp"
#include "validation/firmware/devices/CountingBlockDevice.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include <array>
#include <cstdint>

namespace validation
{
    class BlockBench
        : public Benchmark
    {
    public:
        static constexpr uint32_t maximumBlocks = 128;
        static constexpr uint32_t maximumUnits = 8;

        static bool BlocksFit(const CountingBlockDevice& device, uint32_t blocks, uint32_t repetitions);
        static bool UnitsFit(const CountingBlockDevice& device, uint32_t units, uint32_t repetitions);
        static bool FlushFits(const CountingBlockDevice& device, uint32_t repetitions);
        static bool DiscardFits(const CountingBlockDevice& device, uint32_t blocks, uint32_t repetitions);

        BlockBench(ResultLines& results, uint32_t clockHz);

        void Read(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t blocks, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        void Write(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t blocks, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        void Units(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t units, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        void Flush(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);
        void Discard(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t blocks, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone);

    private:
        using Step = void (BlockBench::*)();

        void Start(CountingBlockDevice& device, const Run& run, uint32_t requests, const infra::Function<void(BenchOutcome outcome)>& onDone);
        infra::Function<void(hal::BlockDevice::Result)> Then(Step next, bool measured);
        void Resume(hal::BlockDevice::Result result, bool measured);
        infra::ByteRange Buffer(uint32_t blocks);
        uint32_t Position() const;

        void ReadNext();
        void WriteNext();
        void UnitNext();
        void FlushNext();
        void FlushWritten();
        void DiscardNext();
        void DiscardWritten();
        void Completed();

    private:
        CountingBlockDevice* device{ nullptr };
        Step resumeWith{ nullptr };
        uint32_t requests{ 0 };
        uint32_t count{ 0 };
        uint32_t blocks{ 0 };
        uint32_t units{ 0 };
        alignas(fs::bufferAlignment) std::array<uint8_t, maximumBlocks * 512> buffer{};
    };
}

#endif
