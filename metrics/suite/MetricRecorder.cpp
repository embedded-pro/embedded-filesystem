#include "metrics/suite/MetricRecorder.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include <algorithm>

namespace metrics
{
    namespace
    {
        bool IsExpected(const CallRecord& record)
        {
            return record.result == fs::Result::success || (record.operation == Operation::expectSize && record.result == fs::Result::unsupported);
        }

        std::string Describe(const CallRecord& record)
        {
            infra::StringOutputStream::WithStorage<32> result;
            result << record.result;
            return std::string{ OperationName(record.operation) } + " in phase '" + record.phase + "' returned " + std::string{ result.Storage().begin(), result.Storage().end() };
        }
    }

    MetricRecorder::MetricRecorder(Report& report, std::string_view subject, std::string_view device, std::size_t ramBytes)
        : report(report)
        , subject(subject)
        , device(device)
        , ramBytes(ramBytes)
    {}

    void MetricRecorder::Record(std::string_view metric, std::string_view workload, std::string_view unit, Better better, double value, std::optional<uint8_t> fill)
    {
        report.Add({ std::string{ metric }, std::string{ workload }, subject, device, std::string{ unit }, fill, value, std::nullopt, ramBytes, better });
    }

    std::optional<std::string> UnexpectedResult(const Runner& runner)
    {
        if (runner.Interrupted())
            return "the run was interrupted";

        const auto& records = runner.Records();
        auto unexpected = std::find_if_not(records.begin(), records.end(), IsExpected);
        if (unexpected == records.end())
            return std::nullopt;

        return Describe(*unexpected);
    }
}
