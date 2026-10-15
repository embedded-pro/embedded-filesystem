#ifndef METRICS_REPORT_REPORT_HPP
#define METRICS_REPORT_REPORT_HPP

#include "metrics/device/DeviceModels.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace metrics
{
    enum class Better : uint8_t
    {
        higher,
        lower
    };

    struct Target
    {
        Better comparison;
        std::variant<double, std::string> reference;
        std::optional<double> factor;

        std::string Text() const;
        std::optional<bool> Holds(double value) const;
    };

    Target AtMost(double bound);
    Target AtLeast(double bound);
    Target AtMostOf(std::string subjects);
    Target AtLeastOf(std::string subjects, std::optional<double> factor = std::nullopt);

    inline constexpr std::string_view allBaselines{ "baselines" };

    struct MetricResult
    {
        std::string metric;
        std::string workload;
        std::string subject;
        std::string device;
        std::string unit;
        std::optional<uint8_t> fill;
        double value;
        std::optional<Target> target;
        std::size_t ramBytes;
        Better better;
        std::optional<bool> pass;
        bool diagnostic{ false };
    };

    class Report
    {
    public:
        const MetricResult& Add(MetricResult result);
        void AddDevice(const NorModel& model);
        void AddDevice(const BlockDeviceModel& model);

        const std::vector<MetricResult>& Results() const;
        std::string Json() const;
        bool WriteTo(const std::string& path) const;

    private:
        using Parameters = std::vector<std::pair<std::string, std::string>>;

        std::map<std::string, Parameters, std::less<>> devices;
        std::vector<MetricResult> results;
    };

    Report& GlobalReport();
}

#endif
