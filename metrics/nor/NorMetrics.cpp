#include "metrics/suite/NorMetrics.hpp"
#include "metrics/nor/NorSubject.hpp"
#include <memory>
#include <set>
#include <string>

namespace metrics
{
    namespace
    {
        constexpr fs::NorConfig norQspi16mConfig{ .maxBlocks = 4096, .programUnit = 1, .pageSize = 256 };
        constexpr fs::NorConfig internalFlashConfig{ .maxBlocks = 256, .programUnit = 16, .pageSize = 16 };

        // Until garbage collection, L4, L5 and the 1000 files of L6 on internal-flash run out of node blocks;
        // until the targets, formatting nor-qspi-16m at maximum timings outlasts the 10 minutes the runner allows a call,
        // and the results are diagnostic, because the erases of the background count in the phase where they happen
        const std::set<std::string, std::less<>> deferredOnNorQspi16m{ "L2_call_latency", "L3_call_latency", "L4_small_files", "L5_static_fill" };
        const std::set<std::string, std::less<>> deferredOnInternalFlash{ "L4_small_files", "L5_static_fill", "L6_reads" };

        template<fs::NorConfig Config>
        NorSetup Setup(std::string label, const NorModel& model, uint32_t scaleDivisor, const std::set<std::string, std::less<>>& deferred)
        {
            return { std::move(label), &model, scaleDivisor, [](VirtualTime&, hal::Flash& flash)
                {
                    return std::make_shared<NorSubject::WithConfig<Config>>(flash);
                },
                deferred, true };
        }
    }

    INSTANTIATE_TEST_SUITE_P(Nor, NorMetrics,
        testing::Values(
            Setup<norQspi16mConfig>("NorQspi16m", norQspi16m, 1, deferredOnNorQspi16m),
            Setup<internalFlashConfig>("InternalFlash", internalFlash, 64, deferredOnInternalFlash)),
        SetupName<NorSetup>);
}
