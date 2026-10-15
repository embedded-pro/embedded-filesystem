#include "metrics/baselines/littlefs/LittleFsSubject.hpp"
#include "metrics/suite/NorMetrics.hpp"
#include <memory>
#include <string>

namespace
{
    const metrics::LittleFsConfiguration ramMatchedOnNorQspi16m = metrics::LittleFsRamMatched(metrics::norQspi16m);
    const metrics::LittleFsConfiguration ramMatchedOnInternalFlash = metrics::LittleFsRamMatched(metrics::internalFlash);
    const metrics::LittleFsConfiguration ramMatchedOnNorQspi1m = metrics::LittleFsRamMatched(metrics::norQspi1m);

    metrics::NorSetup Setup(std::string label, const metrics::LittleFsConfiguration& configuration, const metrics::NorModel& model, uint32_t scaleDivisor)
    {
        return { std::move(label), &model, scaleDivisor, [&configuration](metrics::VirtualTime& time, hal::Flash& flash)
            {
                return std::make_shared<metrics::LittleFsSubject>(time, flash, configuration);
            } };
    }
}

namespace metrics
{
    INSTANTIATE_TEST_SUITE_P(LittleFs, NorMetrics,
        testing::Values(
            Setup("Default_NorQspi16m", metrics::littleFsDefault, metrics::norQspi16m, 1),
            Setup("RamMatched_NorQspi16m", ramMatchedOnNorQspi16m, metrics::norQspi16m, 1),
            Setup("Default_InternalFlash", metrics::littleFsDefault, metrics::internalFlash, 64),
            Setup("RamMatched_InternalFlash", ramMatchedOnInternalFlash, metrics::internalFlash, 64)),
        metrics::SetupName<metrics::NorSetup>);

    INSTANTIATE_TEST_SUITE_P(LittleFs, NorLifetime,
        testing::Values(
            Setup("Default_NorQspi1m", metrics::littleFsDefault, metrics::norQspi1m, 1),
            Setup("RamMatched_NorQspi1m", ramMatchedOnNorQspi1m, metrics::norQspi1m, 1)),
        metrics::SetupName<metrics::NorSetup>);
}
