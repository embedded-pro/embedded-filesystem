#ifndef METRICS_FAT_FAT_SWEEP_HPP
#define METRICS_FAT_FAT_SWEEP_HPP

#include "fs/fat/FatFormatLayout.hpp"
#include "fs/interfaces/Result.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/fat/FatChecker.hpp"
#include "metrics/power_cut/PowerCutSweep.hpp"
#include <optional>

namespace metrics
{
    extern const BlockDeviceModel fatSweep12;
    extern const BlockDeviceModel fatSweep16;
    extern const BlockDeviceModel fatSweep32;

    fs::FatFormatConfig SweepFormat(const BlockDeviceModel& model);
    std::optional<fs::Result> Format(SweepContext<hal::BlockDevice>& context, const fs::FatFormatConfig& config);
    SectorReader ReaderOf(SweepContext<hal::BlockDevice>& context);
}

#endif
