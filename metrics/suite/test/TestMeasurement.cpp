#include "metrics/report/Targets.hpp"
#include "metrics/suite/Measurement.hpp"
#include "metrics/workload/test_doubles/FakeFileSystem.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <memory>

namespace
{
    const metrics::NorModel testNor{ "measurement-nor", 16, 4096, 256, 1, { std::chrono::microseconds(400), std::chrono::milliseconds(45), 50'000'000 },
        { std::chrono::milliseconds(3), std::chrono::milliseconds(400), 50'000'000 } };

    metrics::NorSetup fakeSetup{ "Fake_MeasurementNor", &testNor, 8, [](metrics::VirtualTime&, hal::Flash& flash)
        {
            return std::make_shared<metrics::FakeFileSystem>(flash);
        } };
}

namespace
{
    const metrics::BlockDeviceModel testBlockDevice{ "measurement-block", 512, 64, 16, 2, { std::chrono::microseconds(100), std::chrono::microseconds(25), std::chrono::microseconds(50), {}, {} },
        { std::chrono::microseconds(100), std::chrono::microseconds(25), std::chrono::microseconds(50), std::chrono::milliseconds(250), std::chrono::milliseconds(10) } };

    class BoundTargets
        : public metrics::Targets
    {
    public:
        std::optional<metrics::Target> For(std::string_view metric, std::string_view) const override
        {
            if (metric == "R-2")
                return metrics::AtMost(1);

            return std::nullopt;
        }
    };

    hal::FlashStub flashOfTheFakeSubject{ 16, 4096 };
    const BoundTargets boundTargets;

    metrics::BlockSetup blockSetup{ "Fake_MeasurementBlock", &testBlockDevice, 1, [](metrics::VirtualTime&, hal::BlockDevice&, const metrics::BlockDeviceModel&)
        {
            return std::make_shared<metrics::FakeFileSystem>(flashOfTheFakeSubject);
        },
        &boundTargets };
}

TEST(BlockMeasurementTest, results_carry_the_targets_of_the_setup)
{
    metrics::BlockMeasurement measurement{ blockSetup };

    measurement.Recorder().Record("R-2", "L6 random 65536", "requests", metrics::Better::lower, 1.0);

    const auto& result = metrics::GlobalReport().Results().back();
    ASSERT_TRUE(result.target.has_value());
    EXPECT_EQ("<= 1", result.target->Text());
    EXPECT_EQ("measurement-block", result.device);
}

TEST(NorMeasurementTest, parameters_carry_the_scale_of_the_setup_and_the_fill_level)
{
    metrics::NorMeasurement measurement{ fakeSetup };

    auto parameters = measurement.Parameters(90);

    EXPECT_EQ(8, parameters.scaleDivisor);
    EXPECT_EQ(90, parameters.fillPercent);
}

TEST(NorMeasurementTest, the_subject_runs_on_the_metered_model)
{
    metrics::NorMeasurement measurement{ fakeSetup };

    measurement.Workloads().Format();

    EXPECT_EQ(16, measurement.Flash().NumberOfSectors());
    EXPECT_EQ("measurement-nor", measurement.Flash().Model().name);
    EXPECT_FALSE(measurement.Workloads().Interrupted());
}

TEST(NorMeasurementTest, maximum_timings_are_used_when_asked)
{
    metrics::NorMeasurement measurement{ fakeSetup, metrics::TimingCase::maximum };
    auto file = measurement.Workloads().Open("/file", metrics::OpenMode::write);
    std::array<uint8_t, 16> data{};

    measurement.Workloads().Write(*file, infra::MakeRange(data));

    EXPECT_EQ(std::chrono::milliseconds(3), measurement.Workloads().Records().back().latency);
}

TEST(NorMeasurementTest, results_go_to_the_global_report_with_the_device_described)
{
    metrics::NorMeasurement measurement{ fakeSetup };

    measurement.Recorder().Record("R-4", "L7", "ms", metrics::Better::lower, 1.0, 10);

    const auto& result = metrics::GlobalReport().Results().back();
    EXPECT_EQ("fake", result.subject);
    EXPECT_EQ("measurement-nor", result.device);
    EXPECT_EQ(1234, result.ramBytes);
    EXPECT_THAT(metrics::GlobalReport().Json(), testing::HasSubstr("\"measurement-nor\": {"));
}

TEST(NorMeasurementTest, a_setup_with_targets_attaches_them_to_its_results)
{
    class AtMostTwo
        : public metrics::Targets
    {
    public:
        std::optional<metrics::Target> For(std::string_view, std::string_view) const override
        {
            return metrics::AtMost(2.0);
        }
    } targets;

    auto setup = fakeSetup;
    setup.targets = &targets;
    metrics::NorMeasurement measurement{ setup };

    measurement.Recorder().Record("R-4", "L7", "ms", metrics::Better::lower, 1.0, 10);

    EXPECT_EQ(true, metrics::GlobalReport().Results().back().pass);
}

TEST(NorSetupTest, is_named_after_its_label)
{
    std::ostringstream stream;
    stream << fakeSetup;

    EXPECT_EQ("Fake_MeasurementNor", stream.str());
    EXPECT_EQ("Fake_MeasurementNor", metrics::SetupName(testing::TestParamInfo<metrics::NorSetup>{ fakeSetup, 0 }));
}
