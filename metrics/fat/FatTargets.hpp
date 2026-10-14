#ifndef METRICS_FAT_FAT_TARGETS_HPP
#define METRICS_FAT_FAT_TARGETS_HPP

#include "metrics/device/DeviceModels.hpp"
#include "metrics/report/Targets.hpp"
#include <optional>
#include <string_view>

namespace metrics
{
    class FatTargets
        : public Targets
    {
    public:
        explicit FatTargets(const BlockDeviceModel& model);

        std::optional<Target> For(std::string_view metric, std::string_view workload) const override;

    private:
        std::optional<Target> WriteTarget(std::string_view metric, std::string_view workload) const;
        double CommitMilliseconds() const;

    private:
        const BlockDeviceModel& model;
    };
}

#endif
