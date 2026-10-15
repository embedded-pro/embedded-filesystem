#ifndef METRICS_NOR_NOR_TARGETS_HPP
#define METRICS_NOR_NOR_TARGETS_HPP

#include "fs/nor/NorConfig.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/report/Targets.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace metrics
{
    class NorTargets
        : public Targets
    {
    public:
        NorTargets(const NorModel& model, const fs::NorConfig& config, uint32_t scaleDivisor);

        std::optional<Target> For(std::string_view metric, std::string_view workload) const override;
        double CommitMilliseconds() const;

    private:
        std::optional<Target> WriteTarget(std::string_view metric, std::string_view workload) const;
        std::optional<Target> ReadTarget(std::string_view metric, std::string_view workload) const;
        std::optional<Target> WearTarget(std::string_view metric) const;

    private:
        const NorModel& model;
        fs::NorConfig config;
        uint32_t scaleDivisor;
        std::string firstPhaseOfL1;
    };
}

#endif
