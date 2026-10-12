#include "fs/fat/FatFreeSpaceScan.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    class FatFreeSpaceScanTest
        : public FatVolumeFixture
    {
    public:
        explicit FatFreeSpaceScanTest(uint32_t blocks = 16384, const fs::FatFormatConfig& config = ClusterSize(1))
            : FatVolumeFixture(blocks, config)
        {
            state.geometry = geometry;
        }

        void ExpectStep(fs::Result result = fs::Result::success)
        {
            scan.Step([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        uint32_t ScanAll()
        {
            uint32_t steps = 0;

            for (; !scan.Done(); ++steps)
                ExpectStep();

            return steps;
        }

        uint32_t FatSectorsFor(uint32_t bytesPerEntryTimesTwo) const
        {
            return ((geometry.clusterCount + 2) * bytesPerEntryTimesTwo / 2 + 511) / 512;
        }

        fs::FatVolumeState state;
        fs::FatFreeSpaceScan scan{ cache, state };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatFreeSpaceScanOnFat12Test
        : public FatFreeSpaceScanTest
    {
    public:
        FatFreeSpaceScanOnFat12Test()
            : FatFreeSpaceScanTest(4096, ClusterSize(1))
        {}
    };

    class FatFreeSpaceScanOnFat32Test
        : public FatFreeSpaceScanTest
    {
    public:
        FatFreeSpaceScanOnFat32Test()
            : FatFreeSpaceScanTest(81920, ClusterSize(1))
        {}
    };
}

TEST_F(FatFreeSpaceScanTest, every_cluster_of_a_new_volume_is_free)
{
    ASSERT_EQ(fs::FatType::fat16, geometry.type);
    EXPECT_FALSE(scan.Done());

    EXPECT_EQ(FatSectorsFor(4), ScanAll());

    EXPECT_EQ(geometry.clusterCount, state.freeClusters);
}

TEST_F(FatFreeSpaceScanTest, clusters_of_files_are_used)
{
    std::array<uint8_t, 1500> contents{};
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents), true);
    builder->AddDirectory(0, "", "SUBDIR     ");

    ScanAll();

    EXPECT_EQ(geometry.clusterCount - 4, state.freeClusters);
}

TEST_F(FatFreeSpaceScanTest, each_step_reads_one_fat_sector)
{
    auto reads = device.ReadRequests();

    ExpectStep();
    ExpectStep();

    EXPECT_EQ(reads + 2, device.ReadRequests());
    EXPECT_FALSE(scan.Done());
}

TEST_F(FatFreeSpaceScanTest, restart_counts_from_the_first_sector_again)
{
    ExpectStep();
    ExpectStep();

    scan.Restart();

    EXPECT_EQ(FatSectorsFor(4), ScanAll());
    EXPECT_EQ(geometry.clusterCount, state.freeClusters);
}

TEST_F(FatFreeSpaceScanTest, failed_step_is_reported_and_retried)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectStep(fs::Result::deviceError);

    EXPECT_EQ(FatSectorsFor(4), ScanAll());
    EXPECT_EQ(geometry.clusterCount, state.freeClusters);
}

TEST_F(FatFreeSpaceScanTest, step_is_busy_when_every_cache_sector_is_modified)
{
    for (uint32_t sector = 0; sector != 4; ++sector)
    {
        cache.Modify(geometry.firstDataSector + sector, 0, [](fs::Result, infra::ByteRange) {});
        ExecuteAllActions();
    }

    ExpectStep(fs::Result::busy);
}

TEST_F(FatFreeSpaceScanOnFat12Test, clusters_whose_entry_straddles_two_sectors_count_as_used)
{
    ASSERT_EQ(fs::FatType::fat12, geometry.type);
    uint32_t straddling = 0;

    for (uint32_t cluster = 2; cluster != geometry.clusterCount + 2; ++cluster)
        if (cluster * 3 / 2 % 512 == 511)
            ++straddling;

    EXPECT_EQ(FatSectorsFor(3), ScanAll());

    EXPECT_NE(0u, straddling);
    EXPECT_EQ(geometry.clusterCount - straddling, state.freeClusters);
}

TEST_F(FatFreeSpaceScanOnFat32Test, root_cluster_of_fat32_is_used)
{
    ASSERT_EQ(fs::FatType::fat32, geometry.type);

    EXPECT_EQ(FatSectorsFor(8), ScanAll());

    EXPECT_EQ(geometry.clusterCount - 1, state.freeClusters);
}
