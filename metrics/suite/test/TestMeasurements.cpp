#include "metrics/suite/Measurements.hpp"
#include "metrics/workload/test/FakeSubjectFixture.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <string>

namespace
{
    class MeasurementsTest
        : public testing::Test
        , public metrics::FakeSubjectFixture
    {
    public:
        const metrics::MetricResult& Find(std::string_view metric, std::string_view workload, std::optional<uint8_t> fill = std::nullopt) const
        {
            auto result = std::find_if(report.Results().begin(), report.Results().end(), [&](const metrics::MetricResult& result)
                {
                    return result.metric == metric && result.workload == workload && result.fill == fill;
                });
            EXPECT_NE(report.Results().end(), result) << metric << " " << workload;
            return result != report.Results().end() ? *result : missing;
        }

        metrics::Report report;
        metrics::MetricRecorder recorder{ report, "fake", "fake-nor", 1234 };
        metrics::WorkloadParameters parameters{ .scaleDivisor = 64 };
        metrics::MetricResult missing{};
    };
}

TEST(MeasurementPhasesTest, sequential_write_phases_cover_every_size_and_chunk)
{
    EXPECT_EQ((std::vector<std::string>{ "L1 16384 64", "L1 16384 512", "L1 16384 4096", "L1 65536 64", "L1 65536 512", "L1 65536 4096" }),
        metrics::SequentialWritePhases({ .scaleDivisor = 64 }));
}

TEST(MeasurementPhasesTest, random_overwrite_phases_cover_every_file_size)
{
    EXPECT_EQ((std::vector<std::string>{ "L3 1024", "L3 16384", "L3 65536" }), metrics::RandomOverwritePhases({ .scaleDivisor = 64 }));
    EXPECT_EQ((std::vector<std::string>{ "L3 512", "L3 512", "L3 512" }), metrics::RandomOverwritePhases({ .scaleDivisor = 1u << 20 }));
}

TEST_F(MeasurementsTest, sequential_write_records_throughput_and_amplification_of_every_phase)
{
    metrics::MeasureSequentialWrite(runner, recorder, parameters);

    EXPECT_EQ(12, report.Results().size());
    for (const auto& phase : metrics::SequentialWritePhases(parameters))
    {
        EXPECT_LT(0, Find("W-1", phase).value);
        EXPECT_EQ(metrics::Better::higher, Find("W-1", phase).better);
        EXPECT_DOUBLE_EQ(1.0, Find("W-4", phase).value);
    }
}

TEST_F(MeasurementsTest, sequential_write_throughput_divides_user_bytes_by_device_time)
{
    metrics::MeasureSequentialWrite(runner, recorder, parameters);

    EXPECT_DOUBLE_EQ(4096 / 400e-6 / 4096 * 256, Find("W-1", "L1 16384 4096").value);
}

TEST_F(MeasurementsTest, sequential_write_charges_a_subject_that_counts_one_sector_erase_per_allocated_sector)
{
    subject.counters = metrics::VolumeCounters{};
    subject.countedPerWrite = metrics::VolumeCounters{ .allocatedSectors = 1 };

    metrics::MeasureSequentialWrite(runner, recorder, parameters);

    EXPECT_DOUBLE_EQ(4096 / (400e-6 * 4096 / 256 + 45e-3), Find("W-1", "L1 16384 4096").value);
}

TEST_F(MeasurementsTest, data_logger_of_a_subject_that_counts_averages_whole_checkpoint_periods)
{
    subject.counters = metrics::VolumeCounters{};
    subject.countedPerWrite = metrics::VolumeCounters{ .checkpoints = 1 };

    metrics::MeasureDataLogger(runner, recorder, parameters);

    EXPECT_DOUBLE_EQ(1.0, Find("W-2", "L2", 0).value);
}

TEST_F(MeasurementsTest, random_read_ratio_divides_the_reads_in_the_largest_file_by_those_in_the_smallest)
{
    metrics::MeasureReads(runner, recorder, parameters);
    metrics::MeasureRandomReadRatio(runner, recorder, parameters);

    EXPECT_DOUBLE_EQ(1.0, Find("R-2", "L6 random ratio").value);
    EXPECT_EQ(metrics::Better::lower, Find("R-2", "L6 random ratio").better);
}

TEST_F(MeasurementsTest, data_logger_records_append_scaling_and_amplification)
{
    auto perAppend = metrics::MeasureDataLogger(runner, recorder, parameters);

    EXPECT_EQ(std::chrono::microseconds(400), perAppend);
    EXPECT_DOUBLE_EQ(1.0, Find("W-2", "L2", 0).value);
    EXPECT_DOUBLE_EQ(1.0, Find("W-4", "L2", 0).value);
}

TEST_F(MeasurementsTest, data_logger_results_carry_the_fill_level)
{
    parameters.fillPercent = 50;

    metrics::MeasureDataLogger(runner, recorder, parameters);

    EXPECT_EQ(metrics::Better::lower, Find("W-2", "L2", 50).better);
    EXPECT_EQ(metrics::Better::lower, Find("W-4", "L2", 50).better);
}

TEST_F(MeasurementsTest, fill_scaling_divides_the_append_cost_at_high_fill_by_low_fill)
{
    metrics::MeasureFillScaling(recorder, std::chrono::milliseconds(2), std::chrono::milliseconds(3));

    EXPECT_DOUBLE_EQ(1.5, Find("W-3", "L2").value);
}

TEST_F(MeasurementsTest, data_logger_latency_records_the_99th_percentile_and_the_maximum)
{
    metrics::MeasureDataLoggerLatency(runner, recorder, parameters);

    EXPECT_DOUBLE_EQ(0.4, Find("W-6", "L2 p99", 50).value);
    EXPECT_DOUBLE_EQ(0.4, Find("W-6", "L2 maximum", 50).value);
}

TEST_F(MeasurementsTest, random_overwrite_records_the_overwrite_scaling)
{
    parameters.fillPercent = 10;

    metrics::MeasureRandomOverwrite(runner, recorder, parameters);

    EXPECT_DOUBLE_EQ(1.0, Find("W-5", "L3", 10).value);
}

TEST_F(MeasurementsTest, random_overwrite_latency_records_the_99th_percentile_and_the_maximum)
{
    metrics::MeasureRandomOverwriteLatency(runner, recorder, parameters);

    EXPECT_DOUBLE_EQ(0.8, Find("W-6", "L3 p99", 50).value);
    EXPECT_DOUBLE_EQ(0.8, Find("W-6", "L3 maximum", 50).value);
}

TEST_F(MeasurementsTest, reads_record_throughput_amplification_random_reads_and_lookups)
{
    metrics::MeasureReads(runner, recorder, parameters);

    EXPECT_LT(0, Find("R-1", "L6 sequential").value);
    EXPECT_DOUBLE_EQ(1.0, Find("R-5", "L6 sequential").value);
    for (auto size : { 1024, 16384, 65536 })
        EXPECT_DOUBLE_EQ(1.0, Find("R-2", "L6 random " + std::to_string(size)).value);
    for (auto entries : { 10, 100, 1000 })
        EXPECT_DOUBLE_EQ(0.0, Find("R-3", "L6 lookup " + std::to_string(entries)).value);
}

TEST_F(MeasurementsTest, mount_records_the_mount_time_at_every_fill_level)
{
    metrics::MeasureMount(runner, recorder, parameters);

    for (auto fill : metrics::fillLevels)
        EXPECT_DOUBLE_EQ(0.0, Find("R-4", "L7", fill).value);
}
