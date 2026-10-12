#include "fs/fat/FatBytes.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>

namespace
{
    class FatUsageTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatUsageTest(uint32_t blocks = 16384, const fs::FatFormatConfig& config = ClusterSize(1))
            : FatFileSystemFixture(blocks, config)
        {}

        void StartUsage()
        {
            volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
                {
                    usageDone.callback(result, usage.usedBytes, usage.totalBytes);
                });
        }

        void ExpectUsage(fs::Result result, uint64_t usedClusters)
        {
            const auto& geometry = builder->Geometry();
            uint64_t clusterBytes = geometry.sectorsPerCluster * 512;
            StartUsage();

            if (result == fs::Result::success)
                EXPECT_CALL(usageDone, callback(result, usedClusters * clusterBytes, geometry.clusterCount * clusterBytes));
            else
                EXPECT_CALL(usageDone, callback(result, 0, 0));

            ExecuteAllActions();
        }

        uint32_t ReadsForUsage(uint64_t usedClusters)
        {
            auto before = device.ReadRequests();
            ExpectUsage(fs::Result::success, usedClusters);
            return device.ReadRequests() - before;
        }

        testing::StrictMock<infra::MockCallback<void(fs::Result, uint64_t, uint64_t)>> usageDone;
    };

    class FatUsageWithLargeClustersTest
        : public FatUsageTest
    {
    public:
        FatUsageWithLargeClustersTest()
            : FatUsageTest(65536, ClusterSize(4))
        {}
    };

    class FatUsageOnFat32Test
        : public FatUsageTest
    {
    public:
        FatUsageOnFat32Test()
            : FatUsageTest(81920)
        {}

        void MarkDirty()
        {
            builder->SetFatEntry(1, fs::WithCleanShutdown(fs::FatType::fat32, builder->FatEntry(1), false));
        }

        uint32_t FatSectorsAfterTheFirst() const
        {
            return ((builder->Geometry().clusterCount + 2) * 4 + 511) / 512 - 1;
        }
    };
}

TEST_F(FatUsageTest, first_usage_counts_the_free_clusters)
{
    ExpectMount();

    EXPECT_EQ(((builder->Geometry().clusterCount + 2) * 2 + 511) / 512, ReadsForUsage(0));
}

TEST_F(FatUsageTest, later_usage_is_answered_without_reading)
{
    ExpectMount();
    ReadsForUsage(0);

    EXPECT_EQ(0u, ReadsForUsage(0));
}

TEST_F(FatUsageTest, clusters_of_files_are_used)
{
    std::array<uint8_t, 1500> contents{};
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents));
    ExpectMount();

    ExpectUsage(fs::Result::success, 3);
}

TEST_F(FatUsageTest, usage_is_counted_again_after_a_new_mount)
{
    ExpectMount();
    ReadsForUsage(0);
    ExpectUnmount();
    std::array<uint8_t, 600> contents{};
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents));
    ExpectMount();

    EXPECT_NE(0u, ReadsForUsage(2));
}

TEST_F(FatUsageTest, device_error_is_reported_and_usage_continues_later)
{
    ExpectMount();
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 3);

    ExpectUsage(fs::Result::deviceError, 0);

    ExpectUsage(fs::Result::success, 0);
}

TEST_F(FatUsageTest, usage_beyond_the_path_slots_is_busy)
{
    ExpectMount();

    StartUsage();
    StartUsage();
    StartUsage();

    auto total = uint64_t{ builder->Geometry().clusterCount } * 512;
    EXPECT_CALL(usageDone, callback(fs::Result::busy, 0, 0));
    EXPECT_CALL(usageDone, callback(fs::Result::success, 0, total)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatUsageTest, usage_waiting_behind_a_removal_reports_the_removal)
{
    ExpectMount();
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    StartUsage();

    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    EXPECT_CALL(usageDone, callback(fs::Result::mediaRemoved, 0, 0));
    ExecuteAllActions();
}

TEST_F(FatUsageTest, removed_card_during_usage_faults_the_volume)
{
    ExpectMount();
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent, 2);

    ExpectUsage(fs::Result::mediaRemoved, 0);

    ExpectStat("/", fs::Result::mediaRemoved);
}

TEST_F(FatUsageOnFat32Test, clean_volume_is_answered_from_fsinfo)
{
    ExpectMount();

    EXPECT_EQ(0u, ReadsForUsage(1));
}

TEST_F(FatUsageOnFat32Test, fsinfo_of_a_dirty_volume_is_ignored)
{
    MarkDirty();
    ExpectMount();

    EXPECT_EQ(FatSectorsAfterTheFirst(), ReadsForUsage(1));
}

TEST_F(FatUsageOnFat32Test, fsinfo_with_a_count_out_of_range_is_ignored)
{
    std::array<uint8_t, 512> fsInfo;
    auto sector = *builder->Geometry().fsInfoSector;
    device.ReadBlock(sector, infra::MakeRange(fsInfo));
    fs::StoreLittle32(infra::MakeRange(fsInfo), 488, builder->Geometry().clusterCount + 1);
    device.WriteBlock(sector, infra::MakeRange(fsInfo));
    ExpectMount();

    EXPECT_EQ(FatSectorsAfterTheFirst(), ReadsForUsage(1));
}

TEST_F(FatUsageWithLargeClustersTest, usage_is_counted_in_bytes_of_whole_clusters)
{
    std::array<uint8_t, 5000> contents{};
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents));
    ExpectMount();

    ASSERT_EQ(fs::FatType::fat16, builder->Geometry().type);
    ExpectUsage(fs::Result::success, 3);
}
