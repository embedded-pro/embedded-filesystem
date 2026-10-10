#include "metrics/suite/BlockMetrics.hpp"
#include "metrics/suite/Measurement.hpp"
#include "metrics/suite/Measurements.hpp"
#include "metrics/workload/MetricComputations.hpp"
#include "gtest/gtest.h"

namespace metrics
{
    GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(BlockMetrics);

    namespace
    {
        void RecordWrites(BlockMeasurement& measurement, std::string_view phase, std::optional<uint8_t> fill)
        {
            auto userBytes = TotalUserBytes(Select(measurement.Workloads().Records(), phase, Operation::write));
            const auto& writes = measurement.Writes(phase);
            measurement.Recorder().Record("M-1", phase, "blocks", Better::higher, writes.MeanBlocksPerWrite(), fill);
            measurement.Recorder().Record("M-2", phase, "share", Better::higher, writes.AlignedShare(), fill);
            measurement.Recorder().Record("M-3", phase, "units/MiB", Better::lower, writes.AllocationUnitsPerMebibyte(userBytes), fill);
        }
    }

    TEST_P(BlockMetrics, L1_sequential_write)
    {
        BlockMeasurement measurement{ GetParam() };

        MeasureSequentialWrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());
        for (const auto& phase : SequentialWritePhases(measurement.Parameters()))
            RecordWrites(measurement, phase, std::nullopt);

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(BlockMetrics, L2_data_logger)
    {
        std::vector<infra::Duration> perAppend;
        for (auto fill : fillLevels)
        {
            BlockMeasurement measurement{ GetParam() };
            perAppend.push_back(MeasureDataLogger(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill)));
            RecordWrites(measurement, "L2", fill);
            EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);

            if (fill == fillLevels.back())
                MeasureFillScaling(measurement.Recorder(), perAppend.front(), perAppend.back());
        }
    }

    TEST_P(BlockMetrics, L2_call_latency)
    {
        BlockMeasurement measurement{ GetParam(), TimingCase::maximum };

        MeasureDataLoggerLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(latencyFillLevel));

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(BlockMetrics, L3_random_overwrite)
    {
        for (auto fill : fillLevels)
        {
            BlockMeasurement measurement{ GetParam() };
            MeasureRandomOverwrite(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(fill));
            EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
        }
    }

    TEST_P(BlockMetrics, L3_call_latency)
    {
        BlockMeasurement measurement{ GetParam(), TimingCase::maximum };

        MeasureRandomOverwriteLatency(measurement.Workloads(), measurement.Recorder(), measurement.Parameters(latencyFillLevel));

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(BlockMetrics, L4_small_files)
    {
        for (auto fill : fillLevels)
        {
            BlockMeasurement measurement{ GetParam() };
            SmallFiles(measurement.Workloads(), measurement.Parameters(fill));
            RecordWrites(measurement, "L4", fill);
            EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads())) << "fill " << int(fill);
        }
    }

    TEST_P(BlockMetrics, L6_reads)
    {
        BlockMeasurement measurement{ GetParam() };

        MeasureReads(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }

    TEST_P(BlockMetrics, L7_mount)
    {
        BlockMeasurement measurement{ GetParam() };

        MeasureMount(measurement.Workloads(), measurement.Recorder(), measurement.Parameters());

        EXPECT_EQ(std::nullopt, UnexpectedResult(measurement.Workloads()));
    }
}
