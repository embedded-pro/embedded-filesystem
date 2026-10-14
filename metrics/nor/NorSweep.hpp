#ifndef METRICS_NOR_NOR_SWEEP_HPP
#define METRICS_NOR_NOR_SWEEP_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorConfig.hpp"
#include "hal/interfaces/Flash.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/nor/NorChecker.hpp"
#include "metrics/power_cut/PowerCutSweep.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace metrics
{
    extern const NorModel norSweep128k;
    extern const NorModel internalSweep96k;
    extern const NorModel norSweep128kTwoSectors;

    fs::NorConfig SweepConfig(const NorModel& model);
    NorImageSuperblock SuperblockOf(const NorModel& model, const fs::NorConfig& config);

    std::optional<fs::Result> Format(SweepContext<hal::Flash>& context, const fs::NorConfig& config);
    std::vector<uint8_t> ReadImage(SweepContext<hal::Flash>& context);
    NorCheck CheckImage(SweepContext<hal::Flash>& context, const NorModel& model);
    void Program(SweepContext<hal::Flash>& context, uint32_t address, const std::vector<uint8_t>& bytes);
    void EraseSector(SweepContext<hal::Flash>& context, uint32_t sector);
}

#endif
