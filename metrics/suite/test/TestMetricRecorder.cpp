#include "metrics/suite/MetricRecorder.hpp"
#include "metrics/workload/test/FakeSubjectFixture.hpp"
#include "gmock/gmock.h"

namespace
{
    class MetricRecorderTest
        : public testing::Test
        , public metrics::FakeSubjectFixture
    {
    public:
        metrics::Report report;
        metrics::MetricRecorder recorder{ report, "fake", "fake-nor", 1234 };
    };
}

TEST_F(MetricRecorderTest, record_adds_a_reported_result_for_the_subject_and_device)
{
    recorder.Record("W-2", "L2", "ratio", metrics::Better::lower, 1.05, 50);

    ASSERT_EQ(1, report.Results().size());
    const auto& result = report.Results()[0];
    EXPECT_EQ("W-2", result.metric);
    EXPECT_EQ("L2", result.workload);
    EXPECT_EQ("fake", result.subject);
    EXPECT_EQ("fake-nor", result.device);
    EXPECT_EQ("ratio", result.unit);
    EXPECT_EQ(50, result.fill);
    EXPECT_DOUBLE_EQ(1.05, result.value);
    EXPECT_EQ(std::nullopt, result.target);
    EXPECT_EQ(std::nullopt, result.pass);
    EXPECT_EQ(1234, result.ramBytes);
    EXPECT_EQ(metrics::Better::lower, result.better);
}

TEST_F(MetricRecorderTest, a_clean_run_has_no_unexpected_result)
{
    runner.Format();
    auto file = runner.Open("/file", metrics::OpenMode::write);
    runner.ExpectSize(*file, 10);

    EXPECT_EQ(std::nullopt, metrics::UnexpectedResult(runner));
}

TEST_F(MetricRecorderTest, a_failed_call_is_an_unexpected_result)
{
    runner.SetPhase("L6");
    runner.Open("/missing", metrics::OpenMode::read);

    EXPECT_EQ("open in phase 'L6' returned notFound", metrics::UnexpectedResult(runner));
}

TEST_F(MetricRecorderTest, an_interrupted_run_is_an_unexpected_result)
{
    subject.neverComplete = true;
    runner.Format();

    EXPECT_EQ("the run was interrupted", metrics::UnexpectedResult(runner));
}
