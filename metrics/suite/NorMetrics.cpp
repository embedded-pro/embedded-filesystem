#include "metrics/suite/NorMetrics.hpp"
#include "metrics/device/FlashStatistics.hpp"
#include "metrics/suite/Measurement.hpp"
#include "metrics/suite/Measurements.hpp"
#include "metrics/workload/MetricComputations.hpp"
#include "gtest/gtest.h"
#include <algorithm>

namespace metrics
{
    GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(NorMetrics);
    GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(NorLifetime);

    namespace
    {
        constexpr uint32_t staticFillErasesPerSector = 4;
        constexpr uint32_t endurance = 1000;

        void RecordWear(NorMeasurement& measurement, std::string_view workload, std::optional<uint8_t> fill)
        {
            auto wear = SummarizeWear(measurement.Flash().EraseCounts());
            measurement.Recorder().Record("WL-1", workload, "ratio", Better::lower, wear.Spread(), fill);
            measurement.Recorder().Record("WL-2", workload, "ratio", Better::higher, wear.Efficiency(), fill);
        }

        uint64_t MaximumEraseCount(const MeteredFlash& flash)
        {
            const auto& counts = flash.EraseCounts();
            return *std::max_element(counts.begin(), counts.end());
        }
    }

    TEST_P(NorMetrics, L1_sequential_write)
    {
        NorMeasurement measurement{ GetParam() };

        MeasureSequentialWrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(NorMetrics, L2_data_logger)
    {
        std::vector<infra::Duration> perAppend;
        for (auto fill : fillLevels)
        {
            NorMeasurement measurement{ GetParam() };
            perAppend.push_back(MeasureDataLogger(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill)));
            EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);

            if (fill == fillLevels.back())
                MeasureFillScaling(measurement.Recorder(), perAppend.front(), perAppend.back());
        }
    }

    TEST_P(NorMetrics, L2_call_latency)
    {
        NorMeasurement measurement{ GetParam(), TimingCase::maximum };

        MeasureDataLoggerLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(latencyFillLevel));

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(NorMetrics, L3_random_overwrite)
    {
        for (auto fill : fillLevels)
        {
            NorMeasurement measurement{ GetParam() };
            MeasureRandomOverwrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill));
            EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
        }
    }

    TEST_P(NorMetrics, L3_call_latency)
    {
        NorMeasurement measurement{ GetParam(), TimingCase::maximum };

        MeasureRandomOverwriteLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(latencyFillLevel));

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(NorMetrics, L4_small_files)
    {
        for (auto fill : fillLevels)
        {
            NorMeasurement measurement{ GetParam() };
            SmallFiles(measurement.Workloads(), measurement.Parameters(fill));
            RecordWear(measurement, "L4", fill);
            EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
        }
    }

    TEST_P(NorMetrics, L5_static_fill)
    {
        NorMeasurement measurement{ GetParam() };
        const auto& flash = measurement.Flash();
        auto erases = staticFillErasesPerSector * flash.NumberOfSectors();

        StaticFill(measurement.Workloads(), measurement.Parameters(), [&flash, erases](uint32_t)
            {
                return flash.Counters().sectorsErased < erases;
            });
        RecordWear(measurement, "L5", std::nullopt);

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(NorMetrics, L6_reads)
    {
        NorMeasurement measurement{ GetParam() };

        MeasureReads(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(NorMetrics, L7_mount)
    {
        NorMeasurement measurement{ GetParam() };

        MeasureMount(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(NorLifetime, L5_lifetime)
    {
        NorMeasurement measurement{ GetParam() };
        const auto& flash = measurement.Flash();

        StaticFill(measurement.Workloads(), measurement.Parameters(), [&flash](uint32_t)
            {
                return MaximumEraseCount(flash) < endurance;
            });
        measurement.Recorder().Record("WL-3", "L5", "B", Better::higher, static_cast<double>(TotalUserBytes(Select(measurement.Workloads().Records(), "L5", Operation::write)) + TotalUserBytes(Select(measurement.Workloads().Records(), "fill", Operation::write))));

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }
}
