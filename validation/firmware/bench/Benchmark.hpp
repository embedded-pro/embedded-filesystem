#ifndef VALIDATION_FIRMWARE_BENCH_BENCHMARK_HPP
#define VALIDATION_FIRMWARE_BENCH_BENCHMARK_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/devices/RequestMeter.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/timing/Samples.hpp"
#include <cstdint>

namespace validation
{
    enum class BenchOutcome : uint8_t
    {
        done,
        failed,
        timeout
    };

    // A run of a benchmark: the duration of every measured request, and the record of their statistics
    class Benchmark
    {
    public:
        static constexpr uint32_t maximumRepetitions = 1024;

        static bool Repeatable(uint32_t repetitions);

        bool Running() const;

    protected:
        struct Run
        {
            infra::BoundedConstString device;
            const char* operation;
            uint32_t size;
        };

        Benchmark(ResultLines& results, uint32_t clockHz);
        Benchmark(const Benchmark& other) = delete;
        Benchmark& operator=(const Benchmark& other) = delete;
        ~Benchmark() = default;

        void Begin(RequestMeter& meter, const Run& run, const infra::Function<void(BenchOutcome outcome)>& onDone);
        bool Expired() const;
        void Measure();
        void Stop();
        void Finish(BenchOutcome outcome);
        void Report(const char* extraKey = nullptr, uint32_t extraValue = 0);

    private:
        void PrintRecord(const char* extraKey, uint32_t extraValue) const;
        void TimedOut();

    private:
        ResultLines& results;
        uint32_t clockHz;
        RequestMeter* meter{ nullptr };
        Run run{};
        infra::AutoResetFunction<void(BenchOutcome outcome)> onDone;
        bool running{ false };
        bool expired{ false };
        Samples::WithCapacity<maximumRepetitions> samples;
    };
}

#endif
