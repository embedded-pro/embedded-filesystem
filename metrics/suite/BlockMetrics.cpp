#include "metrics/suite/BlockMetrics.hpp"
#include "metrics/suite/Measurement.hpp"
#include "metrics/suite/Measurements.hpp"
#include "metrics/workload/MetricComputations.hpp"
#include "gtest/gtest.h"

using metrics::BlockMetrics;

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(BlockMetrics);

namespace
{
    void RecordWrites(metrics::BlockMeasurement& measurement, std::string_view phase, std::optional<uint8_t> fill)
    {
        auto userBytes = metrics::TotalUserBytes(metrics::Select(measurement.Workloads().Records(), phase, metrics::Operation::write));
        const auto& writes = measurement.Writes(phase);
        measurement.Recorder().Record("M-1", phase, "blocks", metrics::Better::higher, writes.MeanBlocksPerWrite(), fill);
        measurement.Recorder().Record("M-2", phase, "share", metrics::Better::higher, writes.AlignedShare(), fill);
        measurement.Recorder().Record("M-3", phase, "units/MiB", metrics::Better::lower, writes.AllocationUnitsPerMebibyte(userBytes), fill);
    }
}

TEST_P(BlockMetrics, L1_sequential_write)
{
    metrics::BlockMeasurement measurement{ GetParam() };

    metrics::MeasureSequentialWrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());
    for (const auto& phase : metrics::SequentialWritePhases(measurement.Parameters()))
        RecordWrites(measurement, phase, std::nullopt);

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(BlockMetrics, L2_data_logger)
{
    std::vector<infra::Duration> perAppend;
    for (auto fill : metrics::fillLevels)
    {
        metrics::BlockMeasurement measurement{ GetParam() };
        perAppend.push_back(metrics::MeasureDataLogger(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill)));
        RecordWrites(measurement, "L2", fill);
        EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);

        if (fill == metrics::fillLevels.back())
            metrics::MeasureFillScaling(measurement.Recorder(), perAppend.front(), perAppend.back());
    }
}

TEST_P(BlockMetrics, L2_call_latency)
{
    metrics::BlockMeasurement measurement{ GetParam(), metrics::TimingCase::maximum };

    metrics::MeasureDataLoggerLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(metrics::latencyFillLevel));

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(BlockMetrics, L3_random_overwrite)
{
    for (auto fill : metrics::fillLevels)
    {
        metrics::BlockMeasurement measurement{ GetParam() };
        metrics::MeasureRandomOverwrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill));
        EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
    }
}

TEST_P(BlockMetrics, L3_call_latency)
{
    metrics::BlockMeasurement measurement{ GetParam(), metrics::TimingCase::maximum };

    metrics::MeasureRandomOverwriteLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(metrics::latencyFillLevel));

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(BlockMetrics, L4_small_files)
{
    for (auto fill : metrics::fillLevels)
    {
        metrics::BlockMeasurement measurement{ GetParam() };
        metrics::SmallFiles(measurement.Workloads(), measurement.Parameters(fill));
        RecordWrites(measurement, "L4", fill);
        EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
    }
}

TEST_P(BlockMetrics, L6_reads)
{
    metrics::BlockMeasurement measurement{ GetParam() };

    metrics::MeasureReads(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}

TEST_P(BlockMetrics, L7_mount)
{
    metrics::BlockMeasurement measurement{ GetParam() };

    metrics::MeasureMount(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(measurement.Workloads()));
}
