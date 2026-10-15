#include "metrics/suite/NorMetrics.hpp"
#include "metrics/nor/NorConfigurations.hpp"
#include "metrics/nor/NorSubject.hpp"
#include "metrics/nor/NorTargets.hpp"
#include <memory>
#include <set>
#include <string>

namespace metrics
{
    namespace
    {
        // Formatting nor-qspi-16m at maximum timings takes 27 minutes, longer than the 10 minutes the runner allows a call
        const std::set<std::string, std::less<>> deferredOnNorQspi16m{ "L2_call_latency", "L3_call_latency" };

        const NorTargets norQspi16mTargets{ norQspi16m, norQspi16mConfig, 1 };
        const NorTargets internalFlashTargets{ internalFlash, internalFlashConfig, 64 };
        const NorTargets norQspi1mTargets{ norQspi1m, norQspi1mConfig, 1 };

        template<fs::NorConfig Config>
        NorSetup Setup(std::string label, const NorModel& model, uint32_t scaleDivisor, const std::set<std::string, std::less<>>& deferred, const Targets& targets)
        {
            return { std::move(label), &model, scaleDivisor, [](VirtualTime&, hal::Flash& flash)
                {
                    return std::make_shared<NorSubject::WithConfig<Config>>(flash);
                },
                deferred, &targets };
        }
    }

    INSTANTIATE_TEST_SUITE_P(Nor, NorMetrics,
        testing::Values(
            Setup<norQspi16mConfig>("NorQspi16m", norQspi16m, 1, deferredOnNorQspi16m, norQspi16mTargets),
            Setup<internalFlashConfig>("InternalFlash", internalFlash, 64, {}, internalFlashTargets)),
        SetupName<NorSetup>);

    INSTANTIATE_TEST_SUITE_P(Nor, NorLifetime,
        testing::Values(Setup<norQspi1mConfig>("NorQspi1m", norQspi1m, 1, {}, norQspi1mTargets)),
        SetupName<NorSetup>);
}
