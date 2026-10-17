#include "metrics/device/VirtualTimeEnvironment.hpp"
#include "metrics/nor/NorSweep.hpp"
#include "gtest/gtest.h"
#include <cstdint>
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

TEST_P(NorSweepTest, a_sweep_volume_formats_mounts_and_unmounts)
{
    metrics::NorSweepVolume volume{ context, metrics::SweepConfig(model) };

    EXPECT_EQ(fs::Result::corrupted, volume.Mount());
    EXPECT_EQ(fs::Result::success, volume.Format());
    EXPECT_EQ(fs::Result::success, volume.Mount());
    EXPECT_EQ(fs::Result::success, volume.Unmount());
}

TEST_P(NorSweepTest, a_sweep_volume_makes_directories_and_tells_their_type)
{
    metrics::NorSweepVolume volume{ context, metrics::SweepConfig(model) };
    volume.Format();
    volume.Mount();

    EXPECT_EQ(fs::Result::success, volume.MakeDirectory("/a"));
    EXPECT_EQ(fs::Result::alreadyExists, volume.MakeDirectory("/a"));

    EXPECT_EQ(fs::EntryType::directory, volume.TypeOf("/a"));
    EXPECT_EQ(std::nullopt, volume.TypeOf("/b"));
}

TEST_P(NorSweepTest, a_lite_sweep_volume_reads_and_appends_to_what_the_full_one_wrote_and_makes_no_directories)
{
    auto config = metrics::SweepConfig(model);
    {
        metrics::NorSweepVolume full{ context, config };
        full.Format();
        full.Mount();
        full.OpenFile("/f", fs::OpenMode::write);
        full.Write({ 1, 2, 3 });
        full.Close();
        full.Unmount();
    }

    metrics::NorSweepVolume lite{ context, config, metrics::NorSweepSubject::norLite };
    EXPECT_EQ(fs::Result::success, lite.Mount());
    EXPECT_EQ(fs::Result::unsupported, lite.MakeDirectory("/a"));
    EXPECT_EQ(fs::Result::success, lite.OpenFile("/f", fs::OpenMode::append));
    EXPECT_EQ(fs::Result::success, lite.Write({ 4 }));
    EXPECT_EQ(fs::Result::success, lite.Close());
    EXPECT_EQ((std::vector<uint8_t>{ 1, 2, 3, 4 }), lite.ReadFile("/f"));
    EXPECT_EQ(fs::Result::success, lite.Unmount());
}

INSTANTIATE_TEST_SUITE_P(Models, NorSweepTest, testing::Values(&metrics::norSweep128k, &metrics::internalSweep96k, &metrics::norSweep128kTwoSectors));

TEST(NorSweepModelsTest, the_ram_of_a_model_is_that_of_its_volume_with_one_file)
{
    EXPECT_EQ(sizeof(fs::NorFileSystem::WithStorage<metrics::norSweep128kConfig, 1>), metrics::SweepRamBytes(metrics::norSweep128k, metrics::NorSweepSubject::nor));
    EXPECT_EQ(sizeof(fs::NorFileSystem::WithStorage<metrics::internalSweep96kConfig, 1>), metrics::SweepRamBytes(metrics::internalSweep96k, metrics::NorSweepSubject::nor));
    EXPECT_EQ(sizeof(fs::NorFileSystem::WithStorage<metrics::norSweep128kTwoSectorsConfig, 1>), metrics::SweepRamBytes(metrics::norSweep128kTwoSectors, metrics::NorSweepSubject::nor));
    EXPECT_EQ(sizeof(fs::NorLite::WithStorage<metrics::norSweep128kConfig, 1>), metrics::SweepRamBytes(metrics::norSweep128k, metrics::NorSweepSubject::norLite));
    EXPECT_EQ(sizeof(fs::NorLite::WithStorage<metrics::internalSweep96kConfig, 1>), metrics::SweepRamBytes(metrics::internalSweep96k, metrics::NorSweepSubject::norLite));
    EXPECT_EQ(sizeof(fs::NorLite::WithStorage<metrics::norSweep128kTwoSectorsConfig, 1>), metrics::SweepRamBytes(metrics::norSweep128kTwoSectors, metrics::NorSweepSubject::norLite));
}

TEST(NorSweepModelsTest, each_subject_reports_under_its_own_name)
{
    EXPECT_STREQ("embedded-filesystem/nor", metrics::SubjectName(metrics::NorSweepSubject::nor));
    EXPECT_STREQ("embedded-filesystem/nor-lite", metrics::SubjectName(metrics::NorSweepSubject::norLite));
}

TEST(NorSweepModelsTest, one_model_has_blocks_of_two_sectors)
{
    EXPECT_EQ(1, metrics::SweepConfig(metrics::norSweep128k).sectorsPerBlock);
    EXPECT_EQ(1, metrics::SweepConfig(metrics::internalSweep96k).sectorsPerBlock);
    EXPECT_EQ(2, metrics::SweepConfig(metrics::norSweep128kTwoSectors).sectorsPerBlock);
    EXPECT_EQ(16, metrics::internalSweep96k.programUnit);
}
