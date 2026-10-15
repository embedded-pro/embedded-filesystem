#include "metrics/suite/NorMetrics.hpp"
#include "metrics/device/FlashStatistics.hpp"
#include "metrics/suite/Measurement.hpp"
#include "metrics/suite/Measurements.hpp"
#include "metrics/workload/MetricComputations.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <string_view>

using metrics::NorLifetime;
using metrics::NorMetrics;

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(NorMetrics);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(NorLifetime);

namespace
{
    constexpr uint32_t staticFillErasesPerSector = 4;
    constexpr uint32_t endurance = 1000;

    void RecordWear(metrics::NorMeasurement& measurement, std::string_view workload, std::optional<uint8_t> fill)
    {
        auto wear = metrics::SummarizeWear(measurement.Flash().EraseCounts());
        measurement.Recorder().Record("WL-1", workload, "ratio", metrics::Better::lower, wear.Spread(), fill);
        measurement.Recorder().Record("WL-2", workload, "ratio", metrics::Better::higher, wear.Efficiency(), fill);
    }

    bool Deferred(const metrics::NorSetup& setup)
    {
        std::string_view name = testing::UnitTest::GetInstance()->current_test_info()->name();
        return setup.deferredWorkloads.contains(name.substr(0, name.find('/')));
    }

    uint64_t MaximumEraseCount(const metrics::MeteredFlash& flash)
    {
        const auto& counts = flash.EraseCounts();
        return *std::max_element(counts.begin(), counts.end());
    }
}

TEST_P(NorMetrics, L1_sequential_write)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    metrics::NorMeasurement measurement{ GetParam() };

    metrics::MeasureSequentialWrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(NorMetrics, L2_data_logger)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    std::vector<infra::Duration> perAppend;
    for (auto fill : metrics::fillLevels)
    {
        metrics::NorMeasurement measurement{ GetParam() };
        perAppend.push_back(metrics::MeasureDataLogger(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill)));
        EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);

        if (fill == metrics::fillLevels.back())
            metrics::MeasureFillScaling(measurement.Recorder(), perAppend.front(), perAppend.back());
    }
}

TEST_P(NorMetrics, L2_call_latency)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    metrics::NorMeasurement measurement{ GetParam(), metrics::TimingCase::maximum };

    metrics::MeasureDataLoggerLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(metrics::latencyFillLevel));

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(NorMetrics, L3_random_overwrite)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    for (auto fill : metrics::fillLevels)
    {
        metrics::NorMeasurement measurement{ GetParam() };
        metrics::MeasureRandomOverwrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill));
        EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
    }
}

TEST_P(NorMetrics, L3_call_latency)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    metrics::NorMeasurement measurement{ GetParam(), metrics::TimingCase::maximum };

    metrics::MeasureRandomOverwriteLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(metrics::latencyFillLevel));

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(NorMetrics, L4_small_files)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    for (auto fill : metrics::fillLevels)
    {
        metrics::NorMeasurement measurement{ GetParam() };
        metrics::SmallFiles(measurement.Workloads(), measurement.Parameters(fill));
        RecordWear(measurement, "L4", fill);
        EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
    }
}

TEST_P(NorMetrics, L5_static_fill)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    metrics::NorMeasurement measurement{ GetParam() };
    const auto& flash = measurement.Flash();
    auto erases = staticFillErasesPerSector * flash.NumberOfSectors();

    metrics::StaticFill(measurement.Workloads(), measurement.Parameters(), [&flash, erases](uint32_t)
        {
            return flash.Counters().sectorsErased < erases;
        });
    RecordWear(measurement, "L5", std::nullopt);

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(NorMetrics, L6_reads)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    metrics::NorMeasurement measurement{ GetParam() };

    metrics::MeasureReads(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(NorMetrics, L7_mount)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    metrics::NorMeasurement measurement{ GetParam() };

    metrics::MeasureMount(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(NorLifetime, L5_lifetime)
{
    if (Deferred(GetParam()))
        GTEST_SKIP() << "deferred for " << GetParam().label;

    metrics::NorMeasurement measurement{ GetParam() };
    const auto& flash = measurement.Flash();

    metrics::StaticFill(measurement.Workloads(), measurement.Parameters(), [&flash](uint32_t)
        {
            return MaximumEraseCount(flash) < endurance;
        });
    auto userBytes = metrics::TotalUserBytes(metrics::Select(measurement.Workloads().Records(), "L5", metrics::Operation::write)) + metrics::TotalUserBytes(metrics::Select(measurement.Workloads().Records(), "fill", metrics::Operation::write));
    measurement.Recorder().Record("WL-3", "L5", "B", metrics::Better::higher, static_cast<double>(userBytes));

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}
