#include "metrics/workload/MetricComputations.hpp"
#include "metrics/workload/Pattern.hpp"
#include "metrics/workload/Workloads.hpp"
#include "metrics/workload/test/FakeSubjectFixture.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    class WorkloadsTest
        : public testing::Test
        , public metrics::FakeSubjectFixture
    {
    public:
        metrics::RecordSelection Phase(std::string_view phase, std::optional<metrics::Operation> operation = std::nullopt) const
        {
            return metrics::Select(runner.Records(), phase, operation);
        }

        std::vector<metrics::Operation> Operations(std::string_view phase) const
        {
            std::vector<metrics::Operation> operations;
            for (const auto* record : Phase(phase))
                operations.push_back(record->operation);

            return operations;
        }

        std::size_t FilesIn(std::string_view directory) const
        {
            std::string prefix = std::string{ directory } + "/";
            return std::count_if(subject.files.begin(), subject.files.end(), [&prefix](const auto& file)
                {
                    return file.first.rfind(prefix, 0) == 0;
                });
        }

        bool HoldsPattern(const std::string& path, uint32_t fileId) const
        {
            const auto& content = subject.files.at(path);
            return metrics::MatchesPattern(infra::MakeRange(content), fileId, 0);
        }

        metrics::WorkloadParameters parameters{ .scaleDivisor = 64 };
    };
}

TEST(WorkloadPhaseTest, phase_names_join_the_workload_and_its_values)
{
    EXPECT_EQ("L3 65536", metrics::Phase("L3", 65536));
    EXPECT_EQ("L1 1048576 64", metrics::Phase("L1", 1048576, 64));
}

TEST_F(WorkloadsTest, fill_volume_writes_static_files_up_to_the_fill_level)
{
    runner.Mount();

    EXPECT_EQ(128 * 1024, metrics::FillVolume(runner, 50, parameters, 0));

    EXPECT_EQ(128, FilesIn("/fill"));
    EXPECT_TRUE(HoldsPattern("/fill/0", 1000));
    EXPECT_TRUE(HoldsPattern("/fill/127", 1127));
}

TEST_F(WorkloadsTest, fill_volume_counts_the_space_the_filesystem_already_uses)
{
    subject.files["/existing"] = std::vector<uint8_t>(56 * 1024);

    EXPECT_EQ(72 * 1024, metrics::FillVolume(runner, 50, parameters, 0));
}

TEST_F(WorkloadsTest, fill_volume_queries_the_usage_after_every_file)
{
    metrics::FillVolume(runner, 10, parameters, 0);

    EXPECT_EQ(26, Phase("fill", metrics::Operation::usage).size());
}

TEST_F(WorkloadsTest, fill_volume_stops_when_the_volume_is_full)
{
    subject.unreportedBytes = 200 * 1024;

    EXPECT_EQ(56 * 1024, metrics::FillVolume(runner, 90, parameters, 0));
}

TEST_F(WorkloadsTest, fill_volume_leaves_the_reserved_space_out_of_the_fill_level)
{
    EXPECT_EQ(100 * 1024, metrics::FillVolume(runner, 50, parameters, 56 * 1024));
}

TEST_F(WorkloadsTest, fill_volume_with_everything_reserved_writes_nothing)
{
    EXPECT_EQ(0, metrics::FillVolume(runner, 50, parameters, 1024 * 1024));
}

TEST_F(WorkloadsTest, sequential_write_writes_every_size_in_every_chunk_size)
{
    metrics::SequentialWrite(runner, parameters);

    for (auto size : { 16u * 1024, 64u * 1024 })
        for (auto chunk : { 64u, 512u, 4096u })
        {
            auto writes = Phase(metrics::Phase("L1", size, chunk), metrics::Operation::write);
            EXPECT_EQ(size / chunk, writes.size());
            EXPECT_EQ(size, metrics::TotalUserBytes(writes));
        }

    EXPECT_EQ(0, subject.files.count("/sequential"));
}

TEST_F(WorkloadsTest, sequential_write_announces_the_size_of_every_file)
{
    metrics::SequentialWrite(runner, parameters);

    auto phase = Phase(metrics::Phase("L1", 64 * 1024, 512));
    ASSERT_EQ(metrics::Operation::open, phase[0]->operation);
    EXPECT_EQ(metrics::Operation::expectSize, phase[1]->operation);
}

TEST_F(WorkloadsTest, sequential_write_settles_after_every_file)
{
    metrics::SequentialWrite(runner, parameters);

    EXPECT_EQ(metrics::Operation::idle, Phase(metrics::Phase("L1", 16 * 1024, 64)).back()->operation);
}

TEST_F(WorkloadsTest, data_logger_appends_syncs_and_idles)
{
    parameters.scaleDivisor = 4096;

    metrics::DataLogger(runner, parameters);

    std::vector<metrics::Operation> expected{ metrics::Operation::open };
    for (int append = 0; append != 16; ++append)
        expected.insert(expected.end(), { metrics::Operation::write, metrics::Operation::sync, metrics::Operation::idle });
    expected.push_back(metrics::Operation::close);

    EXPECT_EQ(expected, Operations("L2"));
    EXPECT_EQ(1024, subject.files.at("/log").size());
    EXPECT_TRUE(HoldsPattern("/log", 200));
    EXPECT_EQ(16, subject.idleHints);
}

TEST_F(WorkloadsTest, data_logger_records_the_growing_file_size)
{
    parameters.scaleDivisor = 4096;

    metrics::DataLogger(runner, parameters);

    auto writes = Phase("L2", metrics::Operation::write);
    EXPECT_EQ(0, writes.front()->fileSizeBefore);
    EXPECT_EQ(960, writes.back()->fileSizeBefore);
}

TEST_F(WorkloadsTest, workloads_fill_the_volume_first_when_asked)
{
    parameters.fillPercent = 50;

    metrics::DataLogger(runner, parameters);

    EXPECT_EQ(96 * 1024, metrics::TotalUserBytes(Phase("fill", metrics::Operation::write)));
}

TEST_F(WorkloadsTest, random_overwrite_reserves_its_largest_file_when_filling)
{
    parameters.fillPercent = 50;

    metrics::RandomOverwrite(runner, parameters);

    EXPECT_EQ(96 * 1024, metrics::TotalUserBytes(Phase("fill", metrics::Operation::write)));
}

TEST_F(WorkloadsTest, small_files_reserves_its_window_when_filling)
{
    parameters.fillPercent = 50;

    metrics::SmallFiles(runner, parameters);

    EXPECT_EQ(64 * 1024, metrics::TotalUserBytes(Phase("fill", metrics::Operation::write)));
}

TEST_F(WorkloadsTest, random_overwrite_overwrites_every_file_size_in_place)
{
    metrics::RandomOverwrite(runner, parameters);

    for (auto size : { 1024u, 16u * 1024, 64u * 1024 })
    {
        auto phase = Phase(metrics::Phase("L3", size));
        auto writes = Phase(metrics::Phase("L3", size), metrics::Operation::write);
        EXPECT_EQ(64, writes.size());
        EXPECT_EQ(64, metrics::Count(phase, metrics::Operation::seek));
        EXPECT_EQ(64, metrics::Count(phase, metrics::Operation::sync));
        for (const auto* write : writes)
        {
            EXPECT_EQ(512, write->userBytes);
            EXPECT_EQ(size, write->fileSizeBefore);
        }
    }

    EXPECT_EQ(0, subject.files.count("/overwrite"));
}

TEST_F(WorkloadsTest, small_files_keeps_a_window_of_recent_files)
{
    parameters.scaleDivisor = 10;

    metrics::SmallFiles(runner, parameters);

    EXPECT_EQ(16, FilesIn("/small"));
    EXPECT_EQ(0, subject.files.count("/small/3"));
    EXPECT_TRUE(HoldsPattern("/small/19", 419));
    for (const auto& [path, content] : subject.files)
    {
        EXPECT_GE(content.size(), 100);
        EXPECT_LE(content.size(), 8192);
    }
}

TEST_F(WorkloadsTest, static_fill_fills_70_percent_and_rewrites_the_configuration)
{
    std::vector<uint32_t> asked;

    metrics::StaticFill(runner, parameters, [&asked](uint32_t rewrites)
        {
            asked.push_back(rewrites);
            return rewrites != 5;
        });

    EXPECT_EQ(179, FilesIn("/fill"));
    EXPECT_EQ((std::vector<uint32_t>{ 0, 1, 2, 3, 4, 5 }), asked);
    EXPECT_EQ(5, Phase("L5", metrics::Operation::write).size());
    EXPECT_EQ(4096, subject.files.at("/config").size());
    EXPECT_TRUE(metrics::MatchesPattern(infra::MakeRange(subject.files.at("/config")), 500, 4));
}

TEST_F(WorkloadsTest, read_patterns_reads_sequentially_the_whole_file)
{
    metrics::ReadPatterns(runner, parameters);

    EXPECT_EQ(16 * 1024, metrics::TotalUserBytes(Phase("L6 sequential", metrics::Operation::read)));
}

TEST_F(WorkloadsTest, read_patterns_reads_at_random_offsets_and_at_the_end)
{
    metrics::ReadPatterns(runner, parameters);

    for (auto size : { 1024u, 16u * 1024, 64u * 1024 })
    {
        auto reads = Phase(metrics::Phase("L6 random", size), metrics::Operation::read);
        EXPECT_EQ(64, reads.size());
        EXPECT_EQ(64 * 512, metrics::TotalUserBytes(reads));
        EXPECT_EQ(512, metrics::TotalUserBytes(Phase(metrics::Phase("L6 seek end", size), metrics::Operation::read)));
    }
}

TEST_F(WorkloadsTest, read_patterns_lists_and_looks_up_directories_of_every_size)
{
    metrics::ReadPatterns(runner, parameters);

    for (auto entries : { 10u, 100u, 1000u })
    {
        EXPECT_EQ(entries, FilesIn("/dir" + std::to_string(entries)));
        EXPECT_EQ(1, Phase(metrics::Phase("L6 list", entries), metrics::Operation::list).size());
        auto lookups = Phase(metrics::Phase("L6 lookup", entries), metrics::Operation::open);
        EXPECT_EQ(32, lookups.size());
        for (const auto* lookup : lookups)
            EXPECT_EQ(fs::Result::success, lookup->result);
    }
}

TEST_F(WorkloadsTest, mount_scaling_mounts_once_at_every_fill_level)
{
    metrics::MountScaling(runner, parameters);

    for (auto percent : { 10u, 50u, 90u })
        EXPECT_EQ((std::vector<metrics::Operation>{ metrics::Operation::mount }), Operations(metrics::Phase("L7", percent)));
}

TEST_F(WorkloadsTest, mount_scaling_fills_before_every_mount)
{
    metrics::MountScaling(runner, parameters);

    EXPECT_EQ((25 + 128 + 230) * 1024, metrics::TotalUserBytes(Phase("fill", metrics::Operation::write)));
}

TEST_F(WorkloadsTest, workloads_stop_when_the_runner_is_interrupted)
{
    subject.neverComplete = true;

    metrics::SequentialWrite(runner, parameters);
    metrics::DataLogger(runner, parameters);
    metrics::RandomOverwrite(runner, parameters);
    metrics::SmallFiles(runner, parameters);
    metrics::StaticFill(runner, parameters, [](uint32_t)
        {
            return true;
        });
    metrics::ReadPatterns(runner, parameters);
    metrics::MountScaling(runner, parameters);

    EXPECT_TRUE(runner.Interrupted());
    EXPECT_TRUE(runner.Records().empty());
}
