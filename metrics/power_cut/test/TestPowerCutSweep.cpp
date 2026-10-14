#include "metrics/power_cut/PowerCutSweep.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::NorModel smallNor{ "small-nor", 4, 64, 16, 1, { 100us, 1ms, 50'000'000 }, { 100us, 1ms, 50'000'000 } };
    const metrics::BlockDeviceModel smallBlock{ "small-block", 16, 8, 2, 2, { 10us, 1us, 2us, 0us, 1ms }, { 10us, 1us, 2us, 0us, 1ms } };

    constexpr std::array<uint8_t, 4> record{ 0xab, 0xcd, 0xef, 0x01 };

    bool Program(metrics::NorPowerCutSweep::Context& context, infra::ConstByteRange data, uint32_t address)
    {
        bool done = false;
        context.device.WriteBuffer(data, address, [&done]
            {
                done = true;
            });
        return context.time.RunUntil([&done]
            {
                return done;
            });
    }

    std::array<uint8_t, 5> ReadStore(metrics::NorPowerCutSweep::Context& context)
    {
        std::array<uint8_t, 5> data{};
        bool done = false;
        context.device.ReadBuffer(infra::MakeRange(data), 0, [&done]
            {
                done = true;
            });
        context.time.RunUntil([&done]
            {
                return done;
            });
        return data;
    }

    void WriteRecordThenCommit(metrics::NorPowerCutSweep::Context& context)
    {
        static constexpr std::array<uint8_t, 1> commit{ 0x00 };

        if (Program(context, infra::MakeRange(record), 0))
            Program(context, infra::MakeRange(commit), 4);
    }

    std::optional<std::string> CommittedRecordIsComplete(metrics::NorPowerCutSweep::Context& context)
    {
        auto data = ReadStore(context);
        if (data[4] == 0x00 && !std::equal(record.begin(), record.end(), data.begin()))
            return "committed record is incomplete";

        return std::nullopt;
    }

    std::optional<std::string> RecordIsAbsentOrComplete(metrics::NorPowerCutSweep::Context& context)
    {
        auto data = ReadStore(context);
        bool absent = std::all_of(data.begin(), data.begin() + 4, [](uint8_t byte)
            {
                return byte == 0xff;
            });
        if (!absent && !std::equal(record.begin(), record.end(), data.begin()))
            return "record is torn";

        return std::nullopt;
    }

    void Nothing(metrics::NorPowerCutSweep::Context&)
    {}
}

TEST(PowerCutSweepTest, the_counting_run_reports_the_number_of_write_steps)
{
    metrics::NorPowerCutSweep sweep{ smallNor };

    auto result = sweep.Run(Nothing, WriteRecordThenCommit, CommittedRecordIsComplete);

    EXPECT_EQ(2, result.writeSteps);
}

TEST(PowerCutSweepTest, runs_the_operation_once_per_cut_point_and_mode)
{
    metrics::NorPowerCutSweep sweep{ smallNor };

    auto result = sweep.Run(Nothing, WriteRecordThenCommit, CommittedRecordIsComplete);

    EXPECT_EQ(6, result.runs);
}

TEST(PowerCutSweepTest, a_store_that_commits_after_writing_is_consistent_at_every_cut_point)
{
    metrics::NorPowerCutSweep sweep{ smallNor };

    auto result = sweep.Run(Nothing, WriteRecordThenCommit, CommittedRecordIsComplete);

    EXPECT_EQ(6, result.consistentRuns);
    EXPECT_DOUBLE_EQ(1.0, result.ConsistentShare());
    EXPECT_TRUE(result.failures.empty());
}

TEST(PowerCutSweepTest, lists_the_step_mode_and_reason_of_every_inconsistent_run)
{
    metrics::NorPowerCutSweep sweep{ smallNor, { metrics::NorCutMode::partial } };

    auto result = sweep.Run(Nothing, WriteRecordThenCommit, RecordIsAbsentOrComplete);

    EXPECT_EQ(2, result.runs);
    EXPECT_EQ(1, result.consistentRuns);
    ASSERT_EQ(1, result.failures.size());
    EXPECT_EQ(0, result.failures[0].step);
    EXPECT_EQ("partial", result.failures[0].mode);
    EXPECT_EQ("record is torn", result.failures[0].reason);
    EXPECT_DOUBLE_EQ(0.5, result.ConsistentShare());
}

TEST(PowerCutSweepTest, an_inconsistent_uncut_run_is_reported_without_a_step)
{
    metrics::NorPowerCutSweep sweep{ smallNor };

    auto result = sweep.Run(Nothing, WriteRecordThenCommit, [](metrics::NorPowerCutSweep::Context&) -> std::optional<std::string>
        {
            return "always inconsistent";
        });

    ASSERT_FALSE(result.failures.empty());
    EXPECT_EQ(std::nullopt, result.failures[0].step);
    EXPECT_EQ("uncut", result.failures[0].mode);
}

TEST(PowerCutSweepTest, restores_the_image_after_set_up_before_every_run)
{
    metrics::NorPowerCutSweep sweep{ smallNor };
    std::vector<uint8_t> markersSeen;
    static constexpr std::array<uint8_t, 1> marker{ 0x42 };

    sweep.Run([](metrics::NorPowerCutSweep::Context& context)
        {
            Program(context, infra::MakeRange(marker), 100);
        },
        [&markersSeen](metrics::NorPowerCutSweep::Context& context)
        {
            std::array<uint8_t, 1> data{};
            bool done = false;
            context.device.ReadBuffer(infra::MakeRange(data), 100, [&done]
                {
                    done = true;
                });
            context.time.RunUntil([&done]
                {
                    return done;
                });
            markersSeen.push_back(data[0]);
            WriteRecordThenCommit(context);
        },
        CommittedRecordIsComplete);

    EXPECT_EQ(7, markersSeen.size());
    EXPECT_THAT(markersSeen, testing::Each(0x42));
}

TEST(PowerCutSweepTest, the_operation_sees_its_device_stop_when_power_is_cut)
{
    metrics::NorPowerCutSweep sweep{ smallNor, { metrics::NorCutMode::notStarted } };
    std::vector<bool> firstWriteCompleted;

    sweep.Run(Nothing, [&firstWriteCompleted](metrics::NorPowerCutSweep::Context& context)
        {
            firstWriteCompleted.push_back(Program(context, infra::MakeRange(record), 0));
        },
        CommittedRecordIsComplete);

    EXPECT_EQ((std::vector<bool>{ true, false }), firstWriteCompleted);
}

TEST(PowerCutSweepTest, a_run_that_programs_a_unit_twice_without_an_erase_is_inconsistent)
{
    metrics::NorPowerCutSweep sweep{ smallNor, { metrics::NorCutMode::completed } };

    auto result = sweep.Run(Nothing, [](metrics::NorPowerCutSweep::Context& context)
        {
            if (Program(context, infra::MakeRange(record), 0))
                Program(context, infra::MakeRange(record), 2);
        },
        CommittedRecordIsComplete);

    ASSERT_EQ(2, result.failures.size());
    EXPECT_EQ(std::nullopt, result.failures[0].step);
    EXPECT_EQ("a program unit was programmed twice without an erase", result.failures[0].reason);
    EXPECT_EQ(1, result.failures[1].step);
}

TEST(PowerCutSweepTest, a_unit_that_the_set_up_programmed_counts_as_programmed)
{
    metrics::NorPowerCutSweep sweep{ smallNor, { metrics::NorCutMode::completed } };
    static constexpr std::array<uint8_t, 1> marker{ 0x42 };

    auto result = sweep.Run([](metrics::NorPowerCutSweep::Context& context)
        {
            Program(context, infra::MakeRange(marker), 3);
        },
        WriteRecordThenCommit, CommittedRecordIsComplete);

    ASSERT_FALSE(result.failures.empty());
    EXPECT_EQ("a program unit was programmed twice without an erase", result.failures[0].reason);
}

TEST(PowerCutSweepTest, a_unit_that_reads_erased_after_the_set_up_may_be_programmed)
{
    metrics::NorPowerCutSweep sweep{ smallNor };
    static constexpr std::array<uint8_t, 1> erased{ 0xff };
    static constexpr std::array<uint8_t, 1> marker{ 0x42 };

    auto result = sweep.Run([](metrics::NorPowerCutSweep::Context& context)
        {
            Program(context, infra::MakeRange(erased), 0);
            Program(context, infra::MakeRange(marker), 100);
        },
        WriteRecordThenCommit, CommittedRecordIsComplete);

    EXPECT_TRUE(result.failures.empty());
}

TEST(PowerCutSweepTest, limiting_the_cut_points_samples_them_with_a_deterministic_stride)
{
    EXPECT_EQ((std::vector<uint64_t>{ 0, 1, 2 }), metrics::SampledCutPoints(3, std::nullopt));
    EXPECT_EQ((std::vector<uint64_t>{ 0, 3, 6, 9 }), metrics::SampledCutPoints(10, 4));
    EXPECT_EQ((std::vector<uint64_t>{ 0, 1 }), metrics::SampledCutPoints(2, 4));
}

TEST(PowerCutSweepTest, a_limited_sweep_runs_only_the_sampled_cut_points)
{
    metrics::NorPowerCutSweep sweep{ smallNor, { metrics::NorCutMode::completed } };
    sweep.LimitCutPoints(1);

    auto result = sweep.Run(Nothing, WriteRecordThenCommit, CommittedRecordIsComplete);

    EXPECT_EQ(1, result.runs);
}

namespace
{
    bool WriteBlock(metrics::BlockPowerCutSweep::Context& context, uint32_t block, uint8_t value)
    {
        std::vector<uint8_t> data(16, value);
        bool done = false;
        context.device.WriteBlocks(infra::MakeRange(data), block, [&done](hal::BlockDevice::Result)
            {
                done = true;
            });
        return context.time.RunUntil([&done]
            {
                return done;
            });
    }

    std::vector<uint8_t> ReadBlock(metrics::BlockPowerCutSweep::Context& context, uint32_t block)
    {
        std::vector<uint8_t> data(16);
        bool done = false;
        context.device.ReadBlocks(infra::MakeRange(data), block, [&done](hal::BlockDevice::Result)
            {
                done = true;
            });
        context.time.RunUntil([&done]
            {
                return done;
            });
        return data;
    }

    void WriteDataThenCommitBlock(metrics::BlockPowerCutSweep::Context& context)
    {
        if (WriteBlock(context, 1, 0x11))
            WriteBlock(context, 0, 0xcc);
    }

    std::optional<std::string> CommittedDataIsComplete(metrics::BlockPowerCutSweep::Context& context)
    {
        if (ReadBlock(context, 0) == std::vector<uint8_t>(16, 0xcc) && ReadBlock(context, 1) != std::vector<uint8_t>(16, 0x11))
            return "committed data is incomplete";

        return std::nullopt;
    }

    void NothingOnBlocks(metrics::BlockPowerCutSweep::Context&)
    {}
}

TEST(PowerCutSweepTest, a_block_sweep_runs_the_unwritten_written_and_torn_modes)
{
    metrics::BlockPowerCutSweep sweep{ smallBlock };

    auto result = sweep.Run(NothingOnBlocks, WriteDataThenCommitBlock, CommittedDataIsComplete);

    EXPECT_EQ(2, result.writeSteps);
    EXPECT_EQ(6, result.runs);
    EXPECT_EQ(6, result.consistentRuns);
}

TEST(PowerCutSweepTest, a_block_sweep_finds_a_commit_written_before_its_data)
{
    metrics::BlockPowerCutSweep sweep{ smallBlock, { { 0, metrics::InterruptedBlock::unwritten } } };

    auto result = sweep.Run(NothingOnBlocks, [](metrics::BlockPowerCutSweep::Context& context)
        {
            if (WriteBlock(context, 0, 0xcc))
                WriteBlock(context, 1, 0x11);
        },
        CommittedDataIsComplete);

    ASSERT_EQ(1, result.failures.size());
    EXPECT_EQ(1, result.failures[0].step);
    EXPECT_EQ("unwritten", result.failures[0].mode);
}
