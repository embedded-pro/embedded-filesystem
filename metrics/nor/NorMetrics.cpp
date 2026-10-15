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
        // With blocks of one 2 KiB sector and names of 64 bytes, a block holds 3 nodes of 3 inode records each,
        // and the 1000 files of L6 need more node blocks than the collection can keep at twice the live nodes
        constexpr fs::NorConfig internalFlashConfig{ .maxBlocks = 256, .sectorsPerBlock = 2, .programUnit = 16, .pageSize = 16, .maxNameBytes = 32 };

        // Until the targets, formatting nor-qspi-16m at maximum timings outlasts the 10 minutes the runner allows a call,
        // and the results are diagnostic, because the erases of the background count in the phase where they happen
        const std::set<std::string, std::less<>> deferredOnNorQspi16m{ "L2_call_latency", "L3_call_latency" };
        const std::set<std::string, std::less<>> deferredOnInternalFlash{};

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
