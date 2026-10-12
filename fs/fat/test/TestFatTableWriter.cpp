#include "fs/fat/FatTable.hpp"
#include "fs/fat/FatTableWriter.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    class FatTableWriterTest
        : public FatVolumeFixture
    {
    public:
        explicit FatTableWriterTest(uint32_t blocks = 16384)
            : FatVolumeFixture(blocks, ClusterSize(1))
        {
            cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ geometry.firstFatSector, geometry.sectorsPerFat, geometry.fatCopies });
        }

        void ExpectWrite(uint32_t cluster, uint32_t value, fs::Result result = fs::Result::success)
        {
            writer.Write(cluster, value, [this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        void Flush()
        {
            cache.Flush([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(fs::Result::success));
            ExecuteAllActions();
        }

        uint32_t EntryInCopy(uint8_t copy, uint32_t cluster) const
        {
            auto location = fs::LocateEntry(geometry.type, cluster, 512);
            auto first = geometry.firstFatSector + copy * geometry.sectorsPerFat + location.sector;
            std::array<uint8_t, 512> sector;
            std::array<uint8_t, 512> next;
            device.ReadBlock(first, infra::MakeRange(sector));
            device.ReadBlock(first + 1, infra::MakeRange(next));
            return fs::ReadEntry(geometry.type, cluster, infra::MakeRange(sector), infra::MakeRange(next));
        }

        fs::FatTableWriter writer{ cache, geometry };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatTableWriterOnFat12Test
        : public FatTableWriterTest
    {
    public:
        FatTableWriterOnFat12Test()
            : FatTableWriterTest(4096)
        {}
    };
}

TEST_F(FatTableWriterTest, entry_is_written_to_every_copy_when_the_cache_writes_back)
{
    auto writes = device.WriteRequests();

    ExpectWrite(300, 0x1234);

    EXPECT_EQ(writes, device.WriteRequests());
    Flush();
    EXPECT_EQ(0x1234u, EntryInCopy(0, 300));
    EXPECT_EQ(0x1234u, EntryInCopy(1, 300));
    EXPECT_EQ(0u, EntryInCopy(0, 301));
}

TEST_F(FatTableWriterTest, sector_of_an_entry_is_in_the_active_fat)
{
    EXPECT_EQ(geometry.ActiveFatSector(), writer.SectorOf(2));
    EXPECT_EQ(geometry.ActiveFatSector() + 1, writer.SectorOf(256));
}

TEST_F(FatTableWriterTest, device_error_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ExpectWrite(300, 0x1234, fs::Result::deviceError);
}

TEST_F(FatTableWriterOnFat12Test, entry_that_straddles_two_sectors_is_written_in_both)
{
    ASSERT_EQ(fs::FatType::fat12, geometry.type);
    ASSERT_TRUE(fs::LocateEntry(geometry.type, 341, 512).straddles);
    builder->SetFatEntry(340, 0x123);
    builder->SetFatEntry(342, 0x456);

    ExpectWrite(341, 0xabc);
    Flush();

    EXPECT_EQ(0xabcu, EntryInCopy(0, 341));
    EXPECT_EQ(0xabcu, EntryInCopy(1, 341));
    EXPECT_EQ(0x123u, EntryInCopy(0, 340));
    EXPECT_EQ(0x456u, EntryInCopy(0, 342));
}

TEST_F(FatTableWriterOnFat12Test, error_in_the_second_sector_of_a_straddling_entry_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 1);

    ExpectWrite(341, 0xabc, fs::Result::deviceError);
}
