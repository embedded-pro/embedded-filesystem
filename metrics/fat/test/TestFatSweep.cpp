#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "metrics/device/VirtualTimeEnvironment.hpp"
#include "metrics/fat/FatSweep.hpp"
#include "gtest/gtest.h"
#include <ostream>
#include <vector>

namespace
{
    struct SweepModelCase
    {
        const metrics::BlockDeviceModel& model;
        uint32_t bits;
    };

    void PrintTo(const SweepModelCase& sweepCase, std::ostream* stream)
    {
        *stream << sweepCase.model.name;
    }
}

class FatSweepTest
    : public testing::TestWithParam<SweepModelCase>
{};

TEST_P(FatSweepTest, sweep_model_formats_to_a_consistent_volume_of_its_type)
{
    const auto& model = GetParam().model;
    std::vector<uint8_t> storage(static_cast<std::size_t>(model.numberOfBlocks) * model.blockSize);
    metrics::VirtualTimeEnvironment environment;
    hal::BlockDeviceStub stub{ infra::MakeRange(storage), model.blockSize };
    metrics::SweepContext<hal::BlockDevice> context{ environment.Time(), stub };

    EXPECT_EQ(fs::Result::success, metrics::Format(context, metrics::SweepFormat(model)));

    auto check = metrics::CheckFatImage(metrics::ReaderOf(context), metrics::FatCheckMode::strict);
    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(GetParam().bits, check.layout.bits);
}

INSTANTIATE_TEST_SUITE_P(Models, FatSweepTest, testing::Values(SweepModelCase{ metrics::fatSweep12, 12 }, SweepModelCase{ metrics::fatSweep16, 16 }, SweepModelCase{ metrics::fatSweep32, 32 }));
