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

TEST(NorSetupTest, is_named_after_its_label)
{
    std::ostringstream stream;
    stream << fakeSetup;

    EXPECT_EQ("Fake_MeasurementNor", stream.str());
    EXPECT_EQ("Fake_MeasurementNor", metrics::SetupName(testing::TestParamInfo<metrics::NorSetup>{ fakeSetup, 0 }));
}
