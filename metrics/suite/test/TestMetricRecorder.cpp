#include "metrics/report/Targets.hpp"
#include "metrics/suite/MetricRecorder.hpp"
#include "metrics/workload/test/FakeSubjectFixture.hpp"
#include "gmock/gmock.h"

namespace
{
    class TargetsMock
        : public metrics::Targets
    {
    public:
        MOCK_METHOD(std::optional<metrics::Target>, For, (std::string_view metric, std::string_view workload), (const, override));
    };

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
    EXPECT_FALSE(result.diagnostic);
}

TEST_F(MetricRecorderTest, record_diagnostic_adds_a_result_that_is_not_compared_with_main)
{
    recorder.RecordDiagnostic("P-1", "append torn", "ratio", metrics::Better::higher, 0.5);

    ASSERT_EQ(1, report.Results().size());
    const auto& result = report.Results()[0];
    EXPECT_EQ("P-1", result.metric);
    EXPECT_EQ("append torn", result.workload);
    EXPECT_DOUBLE_EQ(0.5, result.value);
    EXPECT_EQ(std::nullopt, result.fill);
    EXPECT_TRUE(result.diagnostic);
}

TEST_F(MetricRecorderTest, after_record_every_result_as_diagnostic_a_record_is_diagnostic)
{
    recorder.RecordEveryResultAsDiagnostic();

    recorder.Record("W-1", "L1", "B/s", metrics::Better::higher, 1000.0);

    ASSERT_EQ(1, report.Results().size());
    EXPECT_TRUE(report.Results()[0].diagnostic);
    EXPECT_EQ(std::nullopt, report.Results()[0].target);
}

TEST_F(MetricRecorderTest, record_attaches_the_target_of_the_metric_and_workload)
{
    testing::StrictMock<TargetsMock> targets;
    metrics::MetricRecorder withTargets{ report, "fake", "fake-nor", 1234, &targets };
    EXPECT_CALL(targets, For(std::string_view{ "W-2" }, std::string_view{ "L2" })).WillOnce(testing::Return(metrics::AtMost(1.1)));

    withTargets.Record("W-2", "L2", "ratio", metrics::Better::lower, 1.2, 50);

    ASSERT_EQ(1, report.Results().size());
    const auto& result = report.Results()[0];
    ASSERT_TRUE(result.target.has_value());
    EXPECT_EQ("<= 1.1", result.target->Text());
    EXPECT_EQ(false, result.pass);
}

TEST_F(MetricRecorderTest, record_of_a_metric_without_a_target_stays_reported)
{
    testing::StrictMock<TargetsMock> targets;
    metrics::MetricRecorder withTargets{ report, "fake", "fake-nor", 1234, &targets };
    EXPECT_CALL(targets, For(std::string_view{ "R-3" }, std::string_view{ "L6 lookup 10" })).WillOnce(testing::Return(std::nullopt));

    withTargets.Record("R-3", "L6 lookup 10", "ms", metrics::Better::lower, 1.0);

    ASSERT_EQ(1, report.Results().size());
    EXPECT_EQ(std::nullopt, report.Results()[0].target);
}

TEST_F(MetricRecorderTest, record_diagnostic_attaches_no_target)
{
    testing::StrictMock<TargetsMock> targets;
    metrics::MetricRecorder withTargets{ report, "fake", "fake-nor", 1234, &targets };

    withTargets.RecordDiagnostic("P-1", "append torn", "ratio", metrics::Better::higher, 0.5);

    ASSERT_EQ(1, report.Results().size());
    EXPECT_EQ(std::nullopt, report.Results()[0].target);
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
