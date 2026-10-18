#include "validation/firmware/bench/Benchmark.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "validation/firmware/timing/Nanoseconds.hpp"

namespace validation
{
    bool Benchmark::Repeatable(uint32_t repetitions)
    {
        return repetitions >= 1 && repetitions <= maximumRepetitions;
    }

    Benchmark::Benchmark(ResultLines& results, uint32_t clockHz)
        : results(results)
        , clockHz(clockHz)
    {}

    bool Benchmark::Running() const
    {
        return running;
    }

    void Benchmark::Begin(RequestMeter& meter, const Run& run, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(!running);

        this->meter = &meter;
        this->run = run;
        this->onDone = onDone;
        running = true;
        expired = false;
        samples.Clear();
        meter.OnTimeout([this]
            {
                TimedOut();
            });
    }

    bool Benchmark::Expired() const
    {
        return expired;
    }

    void Benchmark::Measure()
    {
        samples.Add(meter->LastCycles());
    }

    void Benchmark::Stop()
    {
        meter->OnTimeout(nullptr);
        running = false;
    }

    void Benchmark::Finish(BenchOutcome outcome)
    {
        Stop();
        onDone(outcome);
    }

    void Benchmark::Report(const char* extraKey, uint32_t extraValue)
    {
        PrintRecord(extraKey, extraValue);
        Finish(BenchOutcome::done);
    }

    void Benchmark::PrintRecord(const char* extraKey, uint32_t extraValue)
    {
        auto record = results.Record("bench");
        record.Add("device", run.device)
            .Add("op", run.operation)
            .Add("size", run.size);

        if (extraKey != nullptr)
            record.Add(extraKey, extraValue);

        record.Add("n", samples.Size())
            .Add("minNs", Nanoseconds(samples.Minimum(), clockHz))
            .Add("medianNs", Nanoseconds(samples.Median(), clockHz))
            .Add("p99Ns", Nanoseconds(samples.Percentile(99), clockHz))
            .Add("maxNs", Nanoseconds(samples.Maximum(), clockHz))
            .Add("meanNs", Nanoseconds(samples.Mean(), clockHz));
    }

    void Benchmark::TimedOut()
    {
        expired = true;
        onDone(BenchOutcome::timeout);
    }
}
