#include "metrics/report/Report.hpp"
#include "metrics/report/ReportEnvironment.hpp"
#include "gmock/gmock.h"

int main(int argc, char** argv)
{
    auto path = metrics::TakeMetricsOutputPath(argc, argv);
    testing::InitGoogleMock(&argc, argv);

    if (path)
        testing::AddGlobalTestEnvironment(new metrics::ReportEnvironment(metrics::GlobalReport(), *path)); // NOSONAR: GoogleTest takes ownership

    return RUN_ALL_TESTS();
}
