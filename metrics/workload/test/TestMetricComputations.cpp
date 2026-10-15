#include "metrics/workload/MetricComputations.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <limits>
#include <optional>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    metrics::CallRecord Record(metrics::Operation operation, std::string phase, metrics::DeviceSnapshot device = {}, uint64_t userBytes = 0, uint32_t fileSizeBefore = 0, infra::Duration latency = {})
    {
        return { operation, std::move(phase), fs::Result::success, userBytes, fileSizeBefore, latency, device };
    }

    metrics::DeviceSnapshot DeviceTime(infra::Duration deviceTime)
    {
        return { .deviceTime = deviceTime };
    }

    metrics::CallRecord Counted(metrics::Operation operation, std::string phase, metrics::DeviceSnapshot device, metrics::VolumeCounters counters, uint32_t fileSizeBefore = 0)
    {
        auto record = Record(operation, std::move(phase), device, 0, fileSizeBefore);
        record.counters = counters;
        return record;
    }

    metrics::CallRecord Checkpoint(metrics::Operation operation, infra::Duration deviceTime, uint32_t fileSizeBefore = 0)
    {
        return Counted(operation, "L2", DeviceTime(deviceTime), { .checkpoints = 1 }, fileSizeBefore);
    }

    std::vector<infra::Duration> Milliseconds(int from, int to)
    {
        std::vector<infra::Duration> values;
        for (int value = from; value <= to; ++value)
            values.push_back(std::chrono::milliseconds(value));

        return values;
    }
}

TEST(MetricComputationsTest, select_keeps_the_records_of_one_phase)
{
    std::vector<metrics::CallRecord> records{ Record(metrics::Operation::write, "a"), Record(metrics::Operation::write, "b"), Record(metrics::Operation::sync, "a") };

    auto selection = metrics::Select(records, "a");

    EXPECT_EQ((metrics::RecordSelection{ &records[0], &records[2] }), selection);
}

TEST(MetricComputationsTest, select_keeps_the_records_of_one_operation)
{
    std::vector<metrics::CallRecord> records{ Record(metrics::Operation::write, "a"), Record(metrics::Operation::sync, "a") };

    auto selection = metrics::Select(records, "a", metrics::Operation::sync);

    EXPECT_EQ((metrics::RecordSelection{ &records[1] }), selection);
}

TEST(MetricComputationsTest, total_device_and_user_bytes_sum_the_selection)
{
    std::vector<metrics::CallRecord> records{
        Record(metrics::Operation::write, "a", { .writeRequests = 1, .bytesWritten = 100, .deviceTime = 1ms }, 64),
        Record(metrics::Operation::write, "a", { .writeRequests = 2, .bytesWritten = 200, .deviceTime = 2ms }, 32),
    };
    auto selection = metrics::Select(records, "a");

    EXPECT_EQ((metrics::DeviceSnapshot{ .writeRequests = 3, .bytesWritten = 300, .deviceTime = 3ms }), metrics::TotalDevice(selection));
    EXPECT_EQ(96, metrics::TotalUserBytes(selection));
}

TEST(MetricComputationsTest, count_counts_one_operation)
{
    std::vector<metrics::CallRecord> records{ Record(metrics::Operation::write, "a"), Record(metrics::Operation::sync, "a"), Record(metrics::Operation::write, "a") };

    EXPECT_EQ(2, metrics::Count(metrics::Select(records, "a"), metrics::Operation::write));
}

TEST(MetricComputationsTest, bytes_per_second_divides_by_the_device_time)
{
    EXPECT_DOUBLE_EQ(1'000'000.0, metrics::BytesPerSecond(1000, 1ms));
}

TEST(MetricComputationsTest, bytes_per_second_without_device_time_is_infinite)
{
    EXPECT_EQ(std::numeric_limits<double>::infinity(), metrics::BytesPerSecond(1000, {}));
    EXPECT_DOUBLE_EQ(0.0, metrics::BytesPerSecond(0, {}));
}

TEST(MetricComputationsTest, amplification_divides_device_bytes_by_user_bytes)
{
    EXPECT_DOUBLE_EQ(1.5, metrics::Amplification(150, 100));
    EXPECT_EQ(std::numeric_limits<double>::infinity(), metrics::Amplification(150, 0));
}

TEST(MetricComputationsTest, ratio_of_two_durations)
{
    EXPECT_DOUBLE_EQ(1.25, metrics::Ratio(5ms, 4ms));
    EXPECT_DOUBLE_EQ(1.0, metrics::Ratio({}, {}));
    EXPECT_EQ(std::numeric_limits<double>::infinity(), metrics::Ratio(1ms, {}));
}

TEST(MetricComputationsTest, ratio_of_two_counts)
{
    EXPECT_DOUBLE_EQ(1.5, metrics::CountRatio(3.0, 2.0));
    EXPECT_DOUBLE_EQ(1.0, metrics::CountRatio(0.0, 0.0));
    EXPECT_EQ(std::numeric_limits<double>::infinity(), metrics::CountRatio(1.0, 0.0));
}

TEST(MetricComputationsTest, latencies_leave_out_idle_periods)
{
    std::vector<metrics::CallRecord> records{ Record(metrics::Operation::write, "a", {}, 0, 0, 1ms), Record(metrics::Operation::idle, "a", {}, 0, 0, 100ms) };

    EXPECT_EQ((std::vector<infra::Duration>{ 1ms }), metrics::Latencies(metrics::Select(records, "a")));
}

TEST(MetricComputationsTest, percentile_uses_the_nearest_rank)
{
    auto values = Milliseconds(1, 100);

    EXPECT_EQ(99ms, metrics::Percentile(values, 0.99));
    EXPECT_EQ(50ms, metrics::Percentile(values, 0.5));
    EXPECT_EQ(100ms, metrics::Percentile(values, 1.0));
    EXPECT_EQ(1ms, metrics::Percentile(values, 0.001));
}

TEST(MetricComputationsTest, percentile_does_not_depend_on_the_order)
{
    std::vector<infra::Duration> values{ 3ms, 1ms, 2ms };

    EXPECT_EQ(2ms, metrics::Percentile(values, 0.5));
}

TEST(MetricComputationsTest, percentile_and_maximum_of_nothing_are_zero)
{
    EXPECT_EQ(infra::Duration{}, metrics::Percentile({}, 0.99));
    EXPECT_EQ(infra::Duration{}, metrics::Maximum({}));
}

TEST(MetricComputationsTest, maximum_is_the_largest_value)
{
    EXPECT_EQ(7ms, metrics::Maximum({ 3ms, 7ms, 2ms }));
}

TEST(MetricComputationsTest, device_time_per_operation_spreads_the_selection_over_the_operations)
{
    std::vector<metrics::CallRecord> records{
        Record(metrics::Operation::seek, "a", DeviceTime(1ms)),
        Record(metrics::Operation::write, "a", DeviceTime(2ms)),
        Record(metrics::Operation::sync, "a", DeviceTime(3ms)),
        Record(metrics::Operation::write, "a", DeviceTime(2ms)),
    };

    EXPECT_EQ(4ms, metrics::DeviceTimePerOperation(metrics::Select(records, "a"), metrics::Operation::write));
    EXPECT_EQ(infra::Duration{}, metrics::DeviceTimePerOperation(metrics::Select(records, "a"), metrics::Operation::read));
}

TEST(MetricComputationsTest, read_requests_per_operation_counts_the_requests_of_seeks_too)
{
    std::vector<metrics::CallRecord> records{
        Record(metrics::Operation::seek, "a", { .readRequests = 2 }),
        Record(metrics::Operation::read, "a", { .readRequests = 1 }),
        Record(metrics::Operation::seek, "a", { .readRequests = 0 }),
        Record(metrics::Operation::read, "a", { .readRequests = 1 }),
    };

    EXPECT_DOUBLE_EQ(2.0, metrics::ReadRequestsPerOperation(metrics::Select(records, "a"), metrics::Operation::read));
}

TEST(MetricComputationsTest, mean_append_device_time_starts_at_the_first_append_from_the_file_size)
{
    std::vector<metrics::CallRecord> records{
        Record(metrics::Operation::write, "L2", DeviceTime(100ms), 64, 0),
        Record(metrics::Operation::write, "L2", DeviceTime(1ms), 64, 64),
        Record(metrics::Operation::write, "L2", DeviceTime(3ms), 64, 128),
    };

    EXPECT_EQ(2ms, metrics::MeanAppendDeviceTime(records, "L2", 64, 2));
}

TEST(MetricComputationsTest, mean_append_device_time_includes_the_sync_and_the_idle_period)
{
    std::vector<metrics::CallRecord> records{
        Record(metrics::Operation::write, "L2", DeviceTime(1ms), 64, 0),
        Record(metrics::Operation::sync, "L2", DeviceTime(2ms)),
        Record(metrics::Operation::idle, "L2", DeviceTime(3ms)),
        Record(metrics::Operation::close, "L2", DeviceTime(50ms)),
    };

    EXPECT_EQ(6ms, metrics::MeanAppendDeviceTime(records, "L2", 0, 1));
}

TEST(MetricComputationsTest, mean_append_device_time_stops_after_the_window)
{
    std::vector<metrics::CallRecord> records{
        Record(metrics::Operation::write, "L2", DeviceTime(1ms), 64, 0),
        Record(metrics::Operation::sync, "L2", DeviceTime(1ms)),
        Record(metrics::Operation::write, "L2", DeviceTime(100ms), 64, 64),
        Record(metrics::Operation::sync, "L2", DeviceTime(100ms)),
    };

    EXPECT_EQ(2ms, metrics::MeanAppendDeviceTime(records, "L2", 0, 1));
}

TEST(MetricComputationsTest, mean_append_device_time_stays_in_its_phase)
{
    std::vector<metrics::CallRecord> records{
        Record(metrics::Operation::write, "L2", DeviceTime(1ms), 64, 0),
        Record(metrics::Operation::write, "other", DeviceTime(100ms), 64, 64),
    };

    EXPECT_EQ(1ms, metrics::MeanAppendDeviceTime(records, "L2", 0, 2));
}

TEST(MetricComputationsTest, mean_append_device_time_without_appends_is_absent)
{
    std::vector<metrics::CallRecord> records{ Record(metrics::Operation::write, "L2", DeviceTime(1ms), 64, 0) };

    EXPECT_EQ(std::nullopt, metrics::MeanAppendDeviceTime(records, "L2", 4096, 2));
}

TEST(MetricComputationsTest, charged_device_time_of_a_subject_that_does_not_count_is_its_device_time)
{
    std::vector<metrics::CallRecord> records{ Record(metrics::Operation::write, "L1", { .deviceTime = 50ms, .eraseTime = 45ms }) };

    EXPECT_EQ(50ms, metrics::ChargedDeviceTime(metrics::Select(records, "L1"), 45ms));
}

TEST(MetricComputationsTest, charged_device_time_replaces_the_erases_by_one_sector_erase_per_allocated_sector)
{
    std::vector<metrics::CallRecord> records{
        Counted(metrics::Operation::write, "L1", { .deviceTime = 50ms, .eraseTime = 45ms }, { .allocatedSectors = 2 }),
        Counted(metrics::Operation::idle, "L1", { .deviceTime = 90ms, .eraseTime = 90ms }, {}),
    };

    EXPECT_EQ(5ms + 90ms, metrics::ChargedDeviceTime(metrics::Select(records, "L1"), 45ms));
}

TEST(MetricComputationsTest, charged_time_per_operation_spreads_the_charged_time_over_the_operations)
{
    std::vector<metrics::CallRecord> records{
        Counted(metrics::Operation::write, "L3", { .deviceTime = 10ms }, { .allocatedSectors = 1 }),
        Counted(metrics::Operation::write, "L3", { .deviceTime = 10ms }, {}),
    };

    EXPECT_EQ(15ms, metrics::ChargedTimePerOperation(metrics::Select(records, "L3"), metrics::Operation::write, 10ms));
}

TEST(MetricComputationsTest, checkpoint_periods_start_after_the_first_checkpoint_from_the_file_size)
{
    std::vector<metrics::CallRecord> records{
        Checkpoint(metrics::Operation::write, 1ms, 0),
        Record(metrics::Operation::write, "L2", DeviceTime(2ms), 64, 64),
        Checkpoint(metrics::Operation::idle, 3ms),
        Record(metrics::Operation::write, "L2", DeviceTime(4ms), 64, 128),
        Checkpoint(metrics::Operation::idle, 5ms),
        Record(metrics::Operation::write, "L2", DeviceTime(6ms), 64, 192),
        Checkpoint(metrics::Operation::idle, 7ms),
        Record(metrics::Operation::write, "L2", DeviceTime(8ms), 64, 256),
    };

    auto window = metrics::CheckpointPeriodsFrom(records, "L2", 64, 2);

    ASSERT_EQ(4, window.size());
    EXPECT_EQ(&records[3], window.front());
    EXPECT_EQ(&records[6], window.back());
}

TEST(MetricComputationsTest, the_last_checkpoint_periods_end_at_the_last_checkpoint_of_the_phase)
{
    std::vector<metrics::CallRecord> records{
        Checkpoint(metrics::Operation::idle, 1ms),
        Record(metrics::Operation::write, "L2", DeviceTime(2ms), 64, 64),
        Checkpoint(metrics::Operation::idle, 3ms),
        Record(metrics::Operation::write, "L2", DeviceTime(4ms), 64, 128),
        Checkpoint(metrics::Operation::idle, 5ms),
        Record(metrics::Operation::write, "L2", DeviceTime(6ms), 64, 192),
        Record(metrics::Operation::close, "other", DeviceTime(9ms)),
    };

    auto window = metrics::LastCheckpointPeriods(records, "L2", 1);

    ASSERT_EQ(2, window.size());
    EXPECT_EQ(&records[3], window.front());
    EXPECT_EQ(&records[4], window.back());
}

TEST(MetricComputationsTest, the_last_checkpoint_periods_count_every_checkpoint_of_a_call)
{
    std::vector<metrics::CallRecord> records{
        Checkpoint(metrics::Operation::idle, 1ms),
        Record(metrics::Operation::write, "L2", DeviceTime(2ms), 64, 64),
        Checkpoint(metrics::Operation::idle, 3ms),
        Record(metrics::Operation::write, "L2", DeviceTime(4ms), 64, 128),
        Counted(metrics::Operation::idle, "L2", DeviceTime(5ms), { .checkpoints = 2 }),
    };

    auto window = metrics::LastCheckpointPeriods(records, "L2", 2);

    ASSERT_EQ(2, window.size());
    EXPECT_EQ(&records[3], window.front());
    EXPECT_EQ(&records[4], window.back());
}

TEST(MetricComputationsTest, checkpoint_periods_count_only_the_checkpoints_of_appends)
{
    std::vector<metrics::CallRecord> records{
        Checkpoint(metrics::Operation::idle, 1ms),
        Record(metrics::Operation::write, "L2", DeviceTime(2ms), 64, 64),
        Checkpoint(metrics::Operation::close, 3ms),
        Record(metrics::Operation::write, "L2", DeviceTime(4ms), 64, 128),
        Checkpoint(metrics::Operation::idle, 5ms),
    };

    auto window = metrics::LastCheckpointPeriods(records, "L2", 1);

    ASSERT_EQ(4, window.size());
    EXPECT_EQ(&records[1], window.front());
    EXPECT_EQ(&records[4], window.back());
    EXPECT_TRUE(metrics::CheckpointPeriodsFrom(records, "L2", 64, 1).empty());
}

TEST(MetricComputationsTest, checkpoint_periods_are_empty_without_enough_checkpoints)
{
    std::vector<metrics::CallRecord> records{
        Checkpoint(metrics::Operation::idle, 1ms),
        Record(metrics::Operation::write, "L2", DeviceTime(2ms), 64, 64),
    };

    EXPECT_TRUE(metrics::CheckpointPeriodsFrom(records, "L2", 0, 1).empty());
    EXPECT_TRUE(metrics::LastCheckpointPeriods(records, "L2", 1).empty());
}

TEST(MetricComputationsTest, mean_append_time_spreads_the_charged_time_of_appends_over_the_writes)
{
    std::vector<metrics::CallRecord> records{
        Counted(metrics::Operation::write, "L2", DeviceTime(2ms), { .allocatedSectors = 1 }),
        Record(metrics::Operation::sync, "L2", DeviceTime(4ms)),
        Record(metrics::Operation::idle, "L2", DeviceTime(6ms)),
        Record(metrics::Operation::write, "L2", DeviceTime(8ms)),
        Record(metrics::Operation::close, "L2", DeviceTime(100ms)),
    };

    EXPECT_EQ(15ms, metrics::MeanAppendTime(metrics::Select(records, "L2"), 10ms));
}

TEST(MetricComputationsTest, mean_append_time_without_writes_is_absent)
{
    EXPECT_EQ(std::nullopt, metrics::MeanAppendTime({}, 10ms));
}
