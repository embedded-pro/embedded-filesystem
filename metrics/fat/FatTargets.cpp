#include "metrics/fat/FatTargets.hpp"
#include "fs/fat/FatFormatLayout.hpp"
#include <chrono>
#include <string>

namespace metrics
{
    namespace
    {
        constexpr std::string_view firstPhaseOfL1{ "L1 1048576 64" };

        std::optional<Target> ReadTarget(std::string_view metric)
        {
            if (metric == "R-1")
                return AtLeastOf(std::string{ allBaselines });

            if (metric == "R-2")
                return AtMost(1);

            if (metric == "R-5")
                return AtMost(1.1);

            return std::nullopt;
        }

        bool ComparedWithFatFs(std::string_view metric, std::string_view workload)
        {
            if (metric == "M-1")
                return workload != "L4";

            return metric == "M-2" && workload.starts_with("L1") && workload != firstPhaseOfL1;
        }

        std::optional<Target> DeviceTarget(std::string_view metric, std::string_view workload)
        {
            if (ComparedWithFatFs(metric, workload))
                return AtLeastOf("fatfs");

            if (metric == "M-3")
                return AtMostOf("fatfs/tuned");

            return std::nullopt;
        }
    }

    FatTargets::FatTargets(const BlockDeviceModel& model)
        : model(model)
    {}

    std::optional<Target> FatTargets::For(std::string_view metric, std::string_view workload) const
    {
        if (metric == "P-1")
            return AtLeast(1.0);

        if (metric.starts_with("R-"))
            return ReadTarget(metric);

        if (metric.starts_with("M-"))
            return DeviceTarget(metric, workload);

        return WriteTarget(metric, workload);
    }

    std::optional<Target> FatTargets::WriteTarget(std::string_view metric, std::string_view workload) const
    {
        if (metric == "W-1")
            return AtLeastOf(std::string{ allBaselines });

        if (metric == "W-2" || (metric == "W-4" && workload.starts_with("L1")))
            return AtMost(1.1);

        if (metric == "W-3")
            return AtMost(1.2);

        if (metric == "W-6" && workload.ends_with(" maximum"))
            return AtMost(CommitMilliseconds());

        return std::nullopt;
    }

    double FatTargets::CommitMilliseconds() const
    {
        const auto& timing = model.Timing(TimingCase::maximum);
        auto writes = WriteTime(model, TimingCase::maximum, 1, 0) * (2 + fs::FatFormatConfig{}.fatCopies);
        auto openings = timing.allocationUnitOpening * 2;
        auto flushes = CommandTime(model, TimingCase::maximum) * 3;
        auto backgroundRead = ReadTime(model, TimingCase::maximum, 1);

        return std::chrono::duration<double, std::milli>(writes + openings + flushes + backgroundRead).count();
    }
}
