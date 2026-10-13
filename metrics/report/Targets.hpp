#ifndef METRICS_REPORT_TARGETS_HPP
#define METRICS_REPORT_TARGETS_HPP

#include "metrics/report/Report.hpp"
#include <optional>
#include <string_view>

namespace metrics
{
    class Targets
    {
    protected:
        Targets() = default;
        Targets(const Targets& other) = delete;
        Targets& operator=(const Targets& other) = delete;
        ~Targets() = default;

    public:
        virtual std::optional<Target> For(std::string_view metric, std::string_view workload) const = 0;
    };
}

#endif
