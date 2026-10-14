#include "metrics/device/VirtualTimeEnvironment.hpp"
#include "metrics/nor/NorSweep.hpp"
#include "gtest/gtest.h"
#include <ostream>
#include <vector>

namespace
{
    void PrintTo(const metrics::NorModel* model, std::ostream* stream)
    {
        *stream << model->name;
    }

    class NorSweepTest
        : public testing::TestWithParam<const metrics::NorModel*>
    {
    public:
        const metrics::NorModel& model{ *GetParam() };
        metrics::VirtualTimeEnvironment environment;
        hal::FlashStub flash{ model.numberOfSectors, model.sectorSize };
        metrics::SweepContext<hal::Flash> context{ environment.Time(), flash };
    };
}

TEST_P(NorSweepTest, sweep_model_has_a_valid_configuration_for_its_device)
{
    auto config = metrics::SweepConfig(model);

    EXPECT_TRUE(fs::IsValidNorConfig(config));
    EXPECT_EQ(model.programUnit, config.programUnit);
    EXPECT_EQ(model.pageSize, config.pageSize);
    EXPECT_EQ(fs::norMinimumCachedNodes, config.cachedNodes);
}

TEST_P(NorSweepTest, sweep_model_formats_to_a_consistent_empty_volume)
{
    auto config = metrics::SweepConfig(model);

    EXPECT_EQ(fs::Result::success, metrics::Format(context, config));

    auto check = metrics::CheckImage(context, model);
    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(metrics::SuperblockOf(model, config), check.superblock);
    EXPECT_EQ(2, check.validSuperblocks);
    EXPECT_TRUE(check.entries.empty());
}

TEST_P(NorSweepTest, programs_and_erases_the_device_synchronously)
{
    metrics::Program(context, 5, { 0x12, 0x34 });

    EXPECT_EQ(0x12, metrics::ReadImage(context)[5]);
    EXPECT_EQ(0x34, metrics::ReadImage(context)[6]);

    metrics::EraseSector(context, 0);

    EXPECT_EQ(0xff, metrics::ReadImage(context)[5]);
    EXPECT_EQ(static_cast<std::size_t>(model.numberOfSectors) * model.sectorSize, metrics::ReadImage(context).size());
}

INSTANTIATE_TEST_SUITE_P(Models, NorSweepTest, testing::Values(&metrics::norSweep128k, &metrics::internalSweep96k, &metrics::norSweep128kTwoSectors));

TEST(NorSweepModelsTest, one_model_has_blocks_of_two_sectors)
{
    EXPECT_EQ(1, metrics::SweepConfig(metrics::norSweep128k).sectorsPerBlock);
    EXPECT_EQ(1, metrics::SweepConfig(metrics::internalSweep96k).sectorsPerBlock);
    EXPECT_EQ(2, metrics::SweepConfig(metrics::norSweep128kTwoSectors).sectorsPerBlock);
    EXPECT_EQ(16, metrics::internalSweep96k.programUnit);
}
