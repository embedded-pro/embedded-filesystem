#include "metrics/baselines/fatfs/FatFsSubject.hpp"
#include "metrics/suite/BlockMetrics.hpp"
#include <memory>

namespace metrics
{
    INSTANTIATE_TEST_SUITE_P(FatFs, BlockMetrics,
        testing::Values(metrics::BlockSetup{ "SdCard256m", &metrics::sdCard256m, 1, [](metrics::VirtualTime& time, hal::BlockDevice& device, const metrics::BlockDeviceModel& model)
            {
                return std::make_shared<metrics::FatFsSubject>(time, device, model.blocksPerAllocationUnit);
            } }),
        metrics::SetupName<metrics::BlockSetup>);
}
