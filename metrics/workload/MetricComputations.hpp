#ifndef METRICS_WORKLOAD_METRIC_COMPUTATIONS_HPP
#define METRICS_WORKLOAD_METRIC_COMPUTATIONS_HPP

#include "metrics/workload/Runner.hpp"
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace metrics
{
    using RecordSelection = std::vector<const CallRecord*>;

    RecordSelection Select(const std::vector<CallRecord>& records, std::string_view phase, std::optional<Operation> operation = std::nullopt);
    DeviceSnapshot TotalDevice(const RecordSelection& records);
    uint64_t TotalUserBytes(const RecordSelection& records);
    std::size_t Count(const RecordSelection& records, Operation operation);

    double BytesPerSecond(uint64_t bytes, infra::Duration deviceTime);
    double Amplification(uint64_t deviceBytes, uint64_t userBytes);
    double Ratio(infra::Duration numerator, infra::Duration denominator);
    double CountRatio(double numerator, double denominator);

    std::vector<infra::Duration> Latencies(const RecordSelection& records);
    infra::Duration Percentile(std::vector<infra::Duration> values, double fraction);
    infra::Duration Maximum(const std::vector<infra::Duration>& values);

    infra::Duration DeviceTimePerOperation(const RecordSelection& records, Operation operation);
    double ReadRequestsPerOperation(const RecordSelection& records, Operation operation);
    std::optional<infra::Duration> MeanAppendDeviceTime(const std::vector<CallRecord>& records, std::string_view phase, uint32_t fromFileSize, std::size_t window);

    infra::Duration ChargedDeviceTime(const RecordSelection& records, infra::Duration sectorErase);
    infra::Duration ChargedTimePerOperation(const RecordSelection& records, Operation operation, infra::Duration sectorErase);
    RecordSelection CheckpointPeriodsFrom(const std::vector<CallRecord>& records, std::string_view phase, uint32_t fromFileSize, std::size_t periods);
    RecordSelection LastCheckpointPeriods(const std::vector<CallRecord>& records, std::string_view phase, std::size_t periods);
    std::optional<infra::Duration> MeanAppendTime(const RecordSelection& records, infra::Duration sectorErase);
}

#endif
