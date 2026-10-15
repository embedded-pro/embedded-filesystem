#include "metrics/report/Report.hpp"
#include "gmock/gmock.h"
#include <fstream>
#include <limits>
#include <sstream>

namespace
{
    metrics::MetricResult Result(double value, std::optional<metrics::Target> target = std::nullopt)
    {
        return { "W-2", "L2", "embedded-filesystem/nor", "nor-qspi-16m", "ratio", 50, value, std::move(target), 7680, metrics::Better::lower };
    }

    std::string ReadFile(const std::string& path)
    {
        std::ifstream file{ path, std::ios::binary };
        std::stringstream content;
        content << file.rdbuf();
        return content.str();
    }

    const metrics::NorModel testNor{ "test-nor", 4, 1024, 256, 1, { std::chrono::microseconds(400), std::chrono::milliseconds(45), 50'000'000 },
        { std::chrono::milliseconds(3), std::chrono::milliseconds(400), 50'000'000 } };
}

TEST(TargetTest, text_shows_the_comparison_and_the_bound)
{
    EXPECT_EQ("<= 1.1", metrics::AtMost(1.1).Text());
    EXPECT_EQ(">= 0.8", metrics::AtLeast(0.8).Text());
}

TEST(TargetTest, text_shows_the_comparison_and_the_reference_subjects)
{
    EXPECT_EQ(">= baselines", metrics::AtLeastOf(std::string{ metrics::allBaselines }).Text());
    EXPECT_EQ("<= fatfs", metrics::AtMostOf("fatfs").Text());
}

TEST(TargetTest, text_shows_the_factor_of_the_reference_subjects)
{
    EXPECT_EQ(">= 0.97 baselines", metrics::AtLeastOf(std::string{ metrics::allBaselines }, 0.97).Text());
}

TEST(TargetTest, an_absolute_bound_holds_up_to_and_including_the_bound)
{
    EXPECT_EQ(true, metrics::AtMost(1.1).Holds(1.1));
    EXPECT_EQ(false, metrics::AtMost(1.1).Holds(1.2));
    EXPECT_EQ(true, metrics::AtLeast(0.8).Holds(0.8));
    EXPECT_EQ(false, metrics::AtLeast(0.8).Holds(0.7));
}

TEST(TargetTest, a_relative_target_is_decided_when_the_reports_are_merged)
{
    EXPECT_EQ(std::nullopt, metrics::AtMostOf("fatfs").Holds(1.0));
}

TEST(ReportTest, add_decides_absolute_targets)
{
    metrics::Report report;

    EXPECT_EQ(true, report.Add(Result(1.0, metrics::AtMost(1.1))).pass);
    EXPECT_EQ(false, report.Add(Result(1.2, metrics::AtMost(1.1))).pass);
}

TEST(ReportTest, add_leaves_relative_and_reported_metrics_undecided)
{
    metrics::Report report;

    EXPECT_EQ(std::nullopt, report.Add(Result(1.0, metrics::AtMostOf("fatfs"))).pass);
    EXPECT_EQ(std::nullopt, report.Add(Result(1.0)).pass);
    EXPECT_EQ(2, report.Results().size());
}

TEST(ReportTest, json_of_an_empty_report)
{
    metrics::Report report;

    EXPECT_EQ("{\n    \"devices\": {},\n    \"results\": []\n}\n", report.Json());
}

TEST(ReportTest, json_holds_every_field_of_a_result)
{
    metrics::Report report;
    report.Add(Result(1.02, metrics::AtMost(1.1)));

    EXPECT_EQ("{\n"
              "    \"devices\": {},\n"
              "    \"results\": [\n"
              "        { \"metric\": \"W-2\", \"workload\": \"L2\", \"subject\": \"embedded-filesystem/nor\", \"device\": \"nor-qspi-16m\", \"unit\": \"ratio\", "
              "\"fill\": 50, \"value\": 1.02, \"target\": \"<= 1.1\", \"pass\": true, \"ramBytes\": 7680, \"better\": \"lower\" }\n"
              "    ]\n"
              "}\n",
        report.Json());
}

TEST(ReportTest, json_leaves_out_missing_fill_target_and_pass)
{
    metrics::Report report;
    auto result = Result(2.5);
    result.fill = std::nullopt;
    result.better = metrics::Better::higher;
    report.Add(result);

    EXPECT_THAT(report.Json(), testing::HasSubstr("\"unit\": \"ratio\", \"value\": 2.5, \"ramBytes\": 7680, \"better\": \"higher\" }"));
}

TEST(ReportTest, json_marks_a_diagnostic)
{
    metrics::Report report;
    auto result = Result(2.5);
    result.better = metrics::Better::higher;
    result.diagnostic = true;
    report.Add(result);

    EXPECT_THAT(report.Json(), testing::HasSubstr("\"better\": \"higher\", \"diagnostic\": true }"));
}

TEST(ReportTest, json_separates_results)
{
    metrics::Report report;
    report.Add(Result(1));
    report.Add(Result(2));

    EXPECT_THAT(report.Json(), testing::HasSubstr("\"better\": \"lower\" },\n        { \"metric\""));
}

TEST(ReportTest, json_writes_infinite_values_as_null)
{
    metrics::Report report;
    report.Add(Result(std::numeric_limits<double>::infinity()));

    EXPECT_THAT(report.Json(), testing::HasSubstr("\"value\": null"));
}

TEST(ReportTest, json_writes_the_shortest_exact_number)
{
    metrics::Report report;
    report.Add(Result(0.1));
    report.Add(Result(123456789.0));

    EXPECT_THAT(report.Json(), testing::HasSubstr("\"value\": 0.1,"));
    EXPECT_THAT(report.Json(), testing::HasSubstr("\"value\": 123456789,"));
}

TEST(ReportTest, json_escapes_strings)
{
    metrics::Report report;
    auto result = Result(1);
    result.subject = "a\"b\\c\n";
    report.Add(result);

    EXPECT_THAT(report.Json(), testing::HasSubstr("\"subject\": \"a\\\"b\\\\c\\u000a\""));
}

TEST(ReportTest, json_describes_every_device_once)
{
    metrics::Report report;
    report.AddDevice(testNor);
    report.AddDevice(testNor);

    EXPECT_THAT(report.Json(), testing::HasSubstr("    \"devices\": {\n        \"test-nor\": {\n            \"sectors\": \"4\",\n            \"sectorSize\": \"1024\","));
    EXPECT_THAT(report.Json(), testing::HasSubstr("\"maximum.readBytesPerSecond\": \"50000000\"\n        }\n    },\n"));
}

TEST(ReportTest, write_to_stores_the_json)
{
    metrics::Report report;
    report.Add(Result(1));
    auto path = testing::TempDir() + "report_test.json";

    EXPECT_TRUE(report.WriteTo(path));

    EXPECT_EQ(report.Json(), ReadFile(path));
}

TEST(ReportTest, write_to_an_unwritable_path_fails)
{
    metrics::Report report;

    EXPECT_FALSE(report.WriteTo(testing::TempDir() + "missing-directory/report.json"));
}

TEST(ReportTest, the_global_report_is_shared)
{
    EXPECT_EQ(&metrics::GlobalReport(), &metrics::GlobalReport());
}
