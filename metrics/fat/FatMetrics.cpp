#include "metrics/fat/FatSubject.hpp"
#include "metrics/fat/FatTargets.hpp"
#include "metrics/suite/BlockMetrics.hpp"
#include <memory>

namespace metrics
{
    namespace
    {
        const FatTargets targetsOnSdCard256m{ sdCard256m };
    }

    INSTANTIATE_TEST_SUITE_P(Fat, BlockMetrics,
        testing::Values(metrics::BlockSetup{ "SdCard256m", &metrics::sdCard256m, 1, [](metrics::VirtualTime&, hal::BlockDevice& device, const metrics::BlockDeviceModel& model)
            {
                return std::make_shared<metrics::FatSubject>(device, model);
            },
            &targetsOnSdCard256m }),
        metrics::SetupName<metrics::BlockSetup>);
}
