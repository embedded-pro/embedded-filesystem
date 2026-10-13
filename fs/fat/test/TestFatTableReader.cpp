#include "fs/fat/FatTable.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"

namespace
{
    class FatTableReaderTest
        : public FatVolumeFixture
    {
    public:
        explicit FatTableReaderTest(const fs::FatFormatConfig& config = ClusterSize(1), uint32_t blocks = 4096)
            : FatVolumeFixture(blocks, config)
        {}

        void ExpectRead(uint32_t cluster, fs::Result result, uint32_t value)
        {
            reader.Read(cluster, [this](fs::Result result, uint32_t value)
                {
                    readDone.callback(result, value);
                });
            EXPECT_CALL(readDone, callback(result, value));
            ExecuteAllActions();
        }

        void ExpectNext(uint32_t cluster, fs::Result result, std::optional<uint32_t> next)
        {
            reader.Next(cluster, [this](fs::Result result, std::optional<uint32_t> next)
                {
                    nextDone.callback(result, next);
                });
            EXPECT_CALL(nextDone, callback(result, next));
            ExecuteAllActions();
        }

        fs::FatTableReader reader{ cache, geometry };
        testing::StrictMock<infra::MockCallback<void(fs::Result, uint32_t)>> readDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<uint32_t>)>> nextDone;
    };
}

TEST_F(FatTableReaderTest, reads_fat_entries)
{
    builder->SetFatEntry(5, 6);
    builder->SetFatEntry(6, 0xfff);

    ExpectRead(5, fs::Result::success, 6);
    ExpectRead(6, fs::Result::success, 0xfff);
    ExpectRead(7, fs::Result::success, 0);
}

TEST_F(FatTableReaderTest, reads_a_fat12_entry_that_straddles_two_sectors)
{
    ASSERT_EQ(fs::FatType::fat12, geometry.type);
    builder->SetFatEntry(341, 0xabc);
    builder->SetFatEntry(340, 0x123);
    builder->SetFatEntry(342, 0x456);

    ExpectRead(341, fs::Result::success, 0xabc);
    ExpectRead(340, fs::Result::success, 0x123);
    ExpectRead(342, fs::Result::success, 0x456);
}

TEST_F(FatTableReaderTest, failure_reading_either_sector_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);
    ExpectRead(341, fs::Result::deviceError, 0);

    cache.Clear();
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 1);
    ExpectRead(341, fs::Result::deviceError, 0);
}

TEST_F(FatTableReaderTest, next_cluster_follows_links_and_ends_at_the_end_of_chain)
{
    builder->SetFatEntry(5, 6);
    builder->SetFatEntry(6, 0xff8);

    ExpectNext(5, fs::Result::success, 6);
    ExpectNext(6, fs::Result::success, std::nullopt);
}

TEST_F(FatTableReaderTest, links_outside_the_clusters_are_corrupted)
{
    builder->SetFatEntry(5, 0);
    builder->SetFatEntry(6, 1);
    builder->SetFatEntry(7, 0xff7);
    builder->SetFatEntry(8, geometry.clusterCount + 2);

    ExpectNext(5, fs::Result::corrupted, std::nullopt);
    ExpectNext(6, fs::Result::corrupted, std::nullopt);
    ExpectNext(7, fs::Result::corrupted, std::nullopt);
    ExpectNext(8, fs::Result::corrupted, std::nullopt);
}

TEST_F(FatTableReaderTest, failure_is_reported_by_next)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ExpectNext(5, fs::Result::mediaRemoved, std::nullopt);
}

namespace
{
    class FatTableReaderOnFat32Test
        : public FatTableReaderTest
    {
    public:
        FatTableReaderOnFat32Test()
            : FatTableReaderTest(ClusterSize(1), 81920)
        {}
    };
}

TEST_F(FatTableReaderOnFat32Test, reads_fat32_entries_without_their_upper_bits)
{
    ASSERT_EQ(fs::FatType::fat32, geometry.type);
    builder->SetFatEntry(200, 0xf0000201);

    ExpectRead(200, fs::Result::success, 0x201);
    ExpectNext(2, fs::Result::success, std::nullopt);
}
