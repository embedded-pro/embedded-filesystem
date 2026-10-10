#include "metrics/workload/MetricComputations.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace metrics
{
    namespace
    {
        double Seconds(infra::Duration duration)
        {
            return std::chrono::duration<double>(duration).count();
        }

        double Quotient(double numerator, double denominator, double whenBothZero)
        {
            if (denominator != 0)
                return numerator / denominator;

            return numerator == 0 ? whenBothZero : std::numeric_limits<double>::infinity();
        }

        bool IsPartOfAppend(Operation operation)
        {
            return operation == Operation::write || operation == Operation::sync || operation == Operation::idle;
        }

        bool StartsAppend(const CallRecord& record, std::string_view phase, uint32_t fromFileSize)
        {
            return record.phase == phase && record.operation == Operation::write && record.fileSizeBefore >= fromFileSize;
        }
    }

    RecordSelection Select(const std::vector<CallRecord>& records, std::string_view phase, std::optional<Operation> operation)
    {
        RecordSelection selection;
        for (const auto& record : records)
            if (record.phase == phase && (!operation || record.operation == *operation))
                selection.push_back(&record);

        return selection;
    }

    DeviceSnapshot TotalDevice(const RecordSelection& records)
    {
        DeviceSnapshot total;
        for (const auto* record : records)
            total = total + record->device;

        return total;
    }

    uint64_t TotalUserBytes(const RecordSelection& records)
    {
        uint64_t total = 0;
        for (const auto* record : records)
            total += record->userBytes;

        return total;
    }

    std::size_t Count(const RecordSelection& records, Operation operation)
    {
        return std::count_if(records.begin(), records.end(), [operation](const CallRecord* record)
            {
                return record->operation == operation;
            });
    }

    double BytesPerSecond(uint64_t bytes, infra::Duration deviceTime)
    {
        return Quotient(static_cast<double>(bytes), Seconds(deviceTime), 0);
    }

    double Amplification(uint64_t deviceBytes, uint64_t userBytes)
    {
        return Quotient(static_cast<double>(deviceBytes), static_cast<double>(userBytes), 0);
    }

    double Ratio(infra::Duration numerator, infra::Duration denominator)
    {
        return Quotient(Seconds(numerator), Seconds(denominator), 1);
    }

    std::vector<infra::Duration> Latencies(const RecordSelection& records)
    {
        std::vector<infra::Duration> latencies;
        for (const auto* record : records)
            if (record->operation != Operation::idle)
                latencies.push_back(record->latency);

        return latencies;
    }

    infra::Duration Percentile(std::vector<infra::Duration> values, double fraction)
    {
        really_assert(fraction > 0 && fraction <= 1);

        if (values.empty())
            return infra::Duration{};

        std::sort(values.begin(), values.end());
        auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())));
        return values[std::max<std::size_t>(rank, 1) - 1];
    }

    infra::Duration Maximum(const std::vector<infra::Duration>& values)
    {
        if (values.empty())
            return infra::Duration{};

        return *std::max_element(values.begin(), values.end());
    }

    infra::Duration DeviceTimePerOperation(const RecordSelection& records, Operation operation)
    {
        auto count = Count(records, operation);
        if (count == 0)
            return infra::Duration{};

        return TotalDevice(records).deviceTime / static_cast<infra::Duration::rep>(count);
    }

    double ReadRequestsPerOperation(const RecordSelection& records, Operation operation)
    {
        return Quotient(static_cast<double>(TotalDevice(records).readRequests), static_cast<double>(Count(records, operation)), 0);
    }

    infra::Duration MeanAppendDeviceTime(const std::vector<CallRecord>& records, std::string_view phase, uint32_t fromFileSize, std::size_t window)
    {
        auto first = std::find_if(records.begin(), records.end(), [phase, fromFileSize](const CallRecord& record)
            {
                return StartsAppend(record, phase, fromFileSize);
            });

        infra::Duration total{};
        std::size_t appends = 0;
        for (auto record = first; record != records.end() && record->phase == phase; ++record)
        {
            if (record->operation == Operation::write && ++appends > window)
                break;

            if (IsPartOfAppend(record->operation))
                total += record->device.deviceTime;
        }

        appends = std::min(appends, window);
        if (appends == 0)
            return infra::Duration{};

        return total / static_cast<infra::Duration::rep>(appends);
    }
}
