#include "metrics/suite/Measurements.hpp"
#include "metrics/workload/MetricComputations.hpp"
#include <algorithm>
#include <chrono>

namespace metrics
{
    namespace
    {
        constexpr uint32_t mebibyte = 1024 * 1024;
        constexpr std::size_t appendWindow = 64;
        constexpr uint32_t smallLogSize = 4096;

        uint32_t Scaled(uint32_t size, const WorkloadParameters& parameters)
        {
            return std::max<uint32_t>(size / parameters.scaleDivisor, 1);
        }

        std::array<uint32_t, 3> OverwriteSizes(const WorkloadParameters& parameters)
        {
            return { std::max(Scaled(64 * 1024, parameters), overwriteSize), std::max(Scaled(mebibyte, parameters), overwriteSize), std::max(Scaled(4 * mebibyte, parameters), overwriteSize) };
        }

        double Milliseconds(infra::Duration duration)
        {
            return std::chrono::duration<double, std::milli>(duration).count();
        }

        void RecordLatency(MetricRecorder& recorder, std::string_view workload, const std::vector<infra::Duration>& latencies)
        {
            recorder.Record("W-6", std::string{ workload } + " p99", "ms", Better::lower, Milliseconds(Percentile(latencies, 0.99)), latencyFillLevel);
            recorder.Record("W-6", std::string{ workload } + " maximum", "ms", Better::lower, Milliseconds(Maximum(latencies)), latencyFillLevel);
        }

        std::vector<infra::Duration> PhaseLatencies(const Runner& runner, const std::vector<std::string>& phases)
        {
            std::vector<infra::Duration> latencies;
            for (const auto& phase : phases)
            {
                auto selected = Latencies(Select(runner.Records(), phase));
                latencies.insert(latencies.end(), selected.begin(), selected.end());
            }

            return latencies;
        }
    }

    std::vector<std::string> SequentialWritePhases(const WorkloadParameters& parameters)
    {
        std::vector<std::string> phases;
        for (auto size : { mebibyte, 4 * mebibyte })
            for (auto chunk : { 64u, 512u, 4096u })
                phases.push_back(Phase("L1", Scaled(size, parameters), chunk));

        return phases;
    }

    std::vector<std::string> RandomOverwritePhases(const WorkloadParameters& parameters)
    {
        std::vector<std::string> phases;
        for (auto size : OverwriteSizes(parameters))
            phases.push_back(Phase("L3", size));

        return phases;
    }

    void MeasureSequentialWrite(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters)
    {
        SequentialWrite(runner, parameters);

        for (const auto& phase : SequentialWritePhases(parameters))
        {
            auto device = TotalDevice(Select(runner.Records(), phase));
            auto userBytes = TotalUserBytes(Select(runner.Records(), phase, Operation::write));
            recorder.Record("W-1", phase, "B/s", Better::higher, BytesPerSecond(userBytes, device.deviceTime));
            recorder.Record("W-4", phase, "ratio", Better::lower, Amplification(device.bytesWritten, userBytes));
        }
    }

    infra::Duration MeasureDataLogger(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters)
    {
        DataLogger(runner, parameters);

        auto logSize = Scaled(4 * mebibyte, parameters);
        auto fullLog = MeanAppendDeviceTime(runner.Records(), "L2", logSize - appendWindow * appendSize, appendWindow);
        auto smallLog = MeanAppendDeviceTime(runner.Records(), "L2", smallLogSize, appendWindow);
        recorder.Record("W-2", "L2", "ratio", Better::lower, Ratio(fullLog, smallLog), parameters.fillPercent);

        auto phase = Select(runner.Records(), "L2");
        auto userBytes = TotalUserBytes(Select(runner.Records(), "L2", Operation::write));
        recorder.Record("W-4", "L2", "ratio", Better::lower, Amplification(TotalDevice(phase).bytesWritten, userBytes), parameters.fillPercent);

        return DeviceTimePerOperation(phase, Operation::write);
    }

    void MeasureFillScaling(MetricRecorder& recorder, infra::Duration perAppendLowFill, infra::Duration perAppendHighFill)
    {
        recorder.Record("W-3", "L2", "ratio", Better::lower, Ratio(perAppendHighFill, perAppendLowFill));
    }

    void MeasureDataLoggerLatency(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters)
    {
        DataLogger(runner, parameters);
        RecordLatency(recorder, "L2", PhaseLatencies(runner, { "L2" }));
    }

    void MeasureRandomOverwrite(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters)
    {
        RandomOverwrite(runner, parameters);

        auto phases = RandomOverwritePhases(parameters);
        auto large = DeviceTimePerOperation(Select(runner.Records(), phases.back()), Operation::write);
        auto small = DeviceTimePerOperation(Select(runner.Records(), phases.front()), Operation::write);
        recorder.Record("W-5", "L3", "ratio", Better::lower, Ratio(large, small), parameters.fillPercent);
    }

    void MeasureRandomOverwriteLatency(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters)
    {
        RandomOverwrite(runner, parameters);
        RecordLatency(recorder, "L3", PhaseLatencies(runner, RandomOverwritePhases(parameters)));
    }

    void MeasureReads(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters)
    {
        ReadPatterns(runner, parameters);
        const auto& records = runner.Records();

        auto sequential = Select(records, "L6 sequential");
        auto bytesRead = TotalUserBytes(Select(records, "L6 sequential", Operation::read));
        recorder.Record("R-1", "L6 sequential", "B/s", Better::higher, BytesPerSecond(bytesRead, TotalDevice(sequential).deviceTime));
        recorder.Record("R-5", "L6 sequential", "ratio", Better::lower, Amplification(TotalDevice(sequential).bytesRead, bytesRead));

        for (auto size : OverwriteSizes(parameters))
            recorder.Record("R-2", Phase("L6 random", size), "requests", Better::lower, ReadRequestsPerOperation(Select(records, Phase("L6 random", size)), Operation::read));

        for (auto entries : { 10u, 100u, 1000u })
            recorder.Record("R-3", Phase("L6 lookup", entries), "ms", Better::lower, Milliseconds(DeviceTimePerOperation(Select(records, Phase("L6 lookup", entries)), Operation::open)));
    }

    void MeasureMount(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters)
    {
        MountScaling(runner, parameters);

        for (auto percent : fillLevels)
            recorder.Record("R-4", "L7", "ms", Better::lower, Milliseconds(TotalDevice(Select(runner.Records(), Phase("L7", percent))).deviceTime), percent);
    }
}
