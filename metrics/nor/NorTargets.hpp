#ifndef METRICS_NOR_NOR_TARGETS_HPP
#define METRICS_NOR_NOR_TARGETS_HPP

#include "metrics/report/Targets.hpp"
#include <optional>
#include <string_view>

namespace metrics
{
    class NorTargets
        : public Targets
    {
    public:
        std::optional<Target> For(std::string_view metric, std::string_view workload) const override;
    };
}

#endif
