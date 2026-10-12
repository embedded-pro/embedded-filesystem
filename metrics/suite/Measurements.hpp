#ifndef METRICS_SUITE_MEASUREMENTS_HPP
#define METRICS_SUITE_MEASUREMENTS_HPP

#include "metrics/suite/MetricRecorder.hpp"
#include "metrics/workload/Runner.hpp"
#include "metrics/workload/Workloads.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace metrics
{
    inline constexpr std::array<uint8_t, 3> fillLevels{ 10, 50, 90 };
    inline constexpr uint8_t latencyFillLevel = 50;

    std::vector<std::string> SequentialWritePhases(const WorkloadParameters& parameters);
    std::vector<std::string> RandomOverwritePhases(const WorkloadParameters& parameters);

    void MeasureSequentialWrite(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters);
    infra::Duration MeasureDataLogger(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters);
    void MeasureFillScaling(MetricRecorder& recorder, infra::Duration perAppendLowFill, infra::Duration perAppendHighFill);
    void MeasureDataLoggerLatency(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters);
    void MeasureRandomOverwrite(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters);
    void MeasureRandomOverwriteLatency(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters);
    void MeasureReads(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters);
    void MeasureMount(Runner& runner, MetricRecorder& recorder, const WorkloadParameters& parameters);
}

#endif
