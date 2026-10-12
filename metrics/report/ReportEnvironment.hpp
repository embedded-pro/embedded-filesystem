#ifndef METRICS_REPORT_REPORT_ENVIRONMENT_HPP
#define METRICS_REPORT_REPORT_ENVIRONMENT_HPP

#include "metrics/report/Report.hpp"
#include "gtest/gtest.h"
#include <optional>
#include <string>

namespace metrics
{
    class ReportEnvironment
        : public testing::Environment
    {
    public:
        ReportEnvironment(const Report& report, std::string path);

        void TearDown() override;

    private:
        const Report& report;
        std::string path;
    };

    std::optional<std::string> TakeMetricsOutputPath(int& argc, char** argv);
}

#endif
