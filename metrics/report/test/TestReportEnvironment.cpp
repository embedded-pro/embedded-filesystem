#include "metrics/report/ReportEnvironment.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include <array>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    std::string ReadFile(const std::string& path)
    {
        std::ifstream file{ path, std::ios::binary };
        std::stringstream content;
        content << file.rdbuf();
        return content.str();
    }
}

TEST(ReportEnvironmentTest, tear_down_writes_the_report)
{
    metrics::Report report;
    report.Add({ "R-4", "L7", "fake", "fake-nor", "s", 10, 0.5, std::nullopt, 0, metrics::Better::lower });
    auto path = testing::TempDir() + "report_environment_test.json";
    metrics::ReportEnvironment environment{ report, path };

    environment.TearDown();

    EXPECT_EQ(report.Json(), ReadFile(path));
}

TEST(ReportEnvironmentTest, tear_down_fails_when_the_report_cannot_be_written)
{
    metrics::Report report;
    metrics::ReportEnvironment environment{ report, testing::TempDir() + "missing-directory/report.json" };

    EXPECT_NONFATAL_FAILURE(environment.TearDown(), "could not be written");
}

TEST(ReportEnvironmentTest, take_metrics_output_path_removes_the_flag)
{
    std::array<std::string, 4> arguments{ "program", "--gtest_brief=1", "--metrics-output=out/metrics.json", "last" };
    std::array<char*, 4> argv{ arguments[0].data(), arguments[1].data(), arguments[2].data(), arguments[3].data() };
    int argc = 4;

    EXPECT_EQ("out/metrics.json", metrics::TakeMetricsOutputPath(argc, argv.data()));

    EXPECT_EQ(3, argc);
    EXPECT_EQ("--gtest_brief=1", std::string{ argv[1] });
    EXPECT_EQ("last", std::string{ argv[2] });
}

TEST(ReportEnvironmentTest, take_metrics_output_path_without_the_flag_keeps_the_arguments)
{
    std::array<std::string, 2> arguments{ "program", "--gtest_brief=1" };
    std::array<char*, 2> argv{ arguments[0].data(), arguments[1].data() };
    int argc = 2;

    EXPECT_EQ(std::nullopt, metrics::TakeMetricsOutputPath(argc, argv.data()));

    EXPECT_EQ(2, argc);
}

TEST(ReportEnvironmentTest, the_program_name_is_never_taken_as_the_flag)
{
    std::array<std::string, 1> arguments{ "--metrics-output=x" };
    std::array<char*, 1> argv{ arguments[0].data() };
    int argc = 1;

    EXPECT_EQ(std::nullopt, metrics::TakeMetricsOutputPath(argc, argv.data()));
    EXPECT_EQ(1, argc);
}
