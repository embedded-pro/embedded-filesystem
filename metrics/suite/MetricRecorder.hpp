#ifndef METRICS_SUITE_METRIC_RECORDER_HPP
#define METRICS_SUITE_METRIC_RECORDER_HPP

#include "metrics/report/Report.hpp"
#include "metrics/report/Targets.hpp"
#include "metrics/workload/Runner.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace metrics
{
    class MetricRecorder
    {
    public:
        MetricRecorder(Report& report, std::string_view subject, std::string_view device, std::size_t ramBytes, const Targets* targets = nullptr);

        void Record(std::string_view metric, std::string_view workload, std::string_view unit, Better better, double value, std::optional<uint8_t> fill = std::nullopt);
        void RecordDiagnostic(std::string_view metric, std::string_view workload, std::string_view unit, Better better, double value, std::optional<uint8_t> fill = std::nullopt);
        void RecordEveryResultAsDiagnostic();

    private:
        Report& report;
        std::string subject;
        std::string device;
        std::size_t ramBytes;
        const Targets* targets;
        bool diagnostic{ false };
    };

    std::optional<std::string> UnexpectedResult(const Runner& runner);
}

#endif
