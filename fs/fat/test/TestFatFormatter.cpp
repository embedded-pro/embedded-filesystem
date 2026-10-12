#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>

namespace
{
    using Sector = std::array<uint8_t, 512>;

    class FatFormatterTestBase
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Format(fs::FatFormatter& formatter, const fs::FatFormatConfig& config = {})
        {
            formatter.Format(config, [this](fs::Result result)
                {
                    done.callback(result);
                });
        }

        void ExpectDone(fs::Result result = fs::Result::success)
        {
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatFormatterOnMockTest
        : public FatFormatterTestBase
    {
    public:
        FatFormatterOnMockTest()
        {
            EXPECT_CALL(device, BlockSize()).WillRepeatedly(testing::Return(512));
            EXPECT_CALL(device, NumberOfBlocks()).WillRepeatedly(testing::Return(4096));
        }

        void ExpectWrite(uint32_t sector, uint32_t sectors)
        {
            EXPECT_CALL(device, WriteBlocks(testing::SizeIs(sectors * 512), sector, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
        }

        void CompleteDevice(hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            ASSERT_TRUE(static_cast<bool>(deviceDone));
            auto onDone = deviceDone;
            deviceDone = nullptr;
            onDone(result);
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        fs::FatFormatter::WithStorage<8> formatter{ device };
        infra::Function<void(hal::BlockDevice::Result)> deviceDone;
    };

    class FatFormatterTest
        : public FatFormatterTestBase
    {
    public:
        Sector Read(uint32_t block) const
        {
            Sector sector;
            device.ReadBlock(block, infra::MakeRange(sector));
            return sector;
        }

        fs::FatGeometry Geometry() const
        {
            auto partition = fs::FirstFatPartition(infra::MakeRange(Read(0)));
            auto start = partition ? partition->firstSector : 0;
            auto parameters = fs::ParseBootSector(infra::MakeRange(Read(start)));
            EXPECT_TRUE(parameters.has_value());
            auto geometry = fs::GeometryOf(*parameters, start, device.NumberOfBlocks() - start, 512);
            EXPECT_EQ(fs::Result::success, geometry.result);
            return geometry.geometry;
        }

        uint32_t FatEntry(const fs::FatGeometry& geometry, uint8_t copy, uint32_t cluster) const
        {
            auto location = fs::LocateEntry(geometry.type, cluster, 512);
            return fs::ReadEntry(geometry.type, cluster, infra::MakeRange(Read(geometry.firstFatSector + copy * geometry.sectorsPerFat + location.sector)), infra::ConstByteRange());
        }

        bool IsZero(uint32_t begin, uint32_t end) const
        {
            for (auto block = begin; block != end; ++block)
                if (Read(block) != Sector{})
                    return false;

            return true;
        }

        void Junk(uint32_t begin, uint32_t end)
        {
            Sector junk;
            junk.fill(0xa5);

            for (auto block = begin; block != end; ++block)
                device.WriteBlock(block, infra::MakeRange(junk));
        }

        fs::SparseBlockDevice::WithStorage<64> device{ 81920 };
        fs::FatFormatter::WithStorage<8> formatter{ device };
    };
}

TEST_F(FatFormatterOnMockTest, writes_the_metadata_in_order_then_discards_and_flushes)
{
    testing::InSequence sequence;
    ExpectWrite(0, 1);
    ExpectWrite(13, 1);
    ExpectWrite(14, 1);
    ExpectWrite(15, 1);
    ExpectWrite(16, 8);
    ExpectWrite(24, 8);
    ExpectWrite(32, 8);
    ExpectWrite(40, 8);
    EXPECT_CALL(device, EraseBlocks(48, 4096, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&deviceDone));

    Format(formatter);

    for (int request = 0; request != 10; ++request)
        CompleteDevice();

    ExpectDone();
}

TEST_F(FatFormatterOnMockTest, device_error_stops_the_format_and_is_reported)
{
    testing::InSequence sequence;
    ExpectWrite(0, 1);
    ExpectWrite(13, 1);

    Format(formatter);
    CompleteDevice();
    CompleteDevice(hal::BlockDevice::Result::writeProtected);

    ExpectDone(fs::Result::readOnly);
}

TEST_F(FatFormatterOnMockTest, failed_discard_is_reported)
{
    testing::InSequence sequence;

    for (uint32_t sector : { 0, 13, 14, 15 })
        ExpectWrite(sector, 1);

    for (uint32_t sector : { 16, 24, 32, 40 })
        ExpectWrite(sector, 8);

    EXPECT_CALL(device, EraseBlocks(48, 4096, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));

    Format(formatter);

    for (int request = 0; request != 8; ++request)
        CompleteDevice();

    CompleteDevice(hal::BlockDevice::Result::timeout);
    ExpectDone(fs::Result::deviceError);
}

TEST_F(FatFormatterOnMockTest, failed_flush_is_reported)
{
    testing::InSequence sequence;

    for (uint32_t sector : { 0, 13, 14, 15 })
        ExpectWrite(sector, 1);

    for (uint32_t sector : { 16, 24, 32, 40 })
        ExpectWrite(sector, 8);

    EXPECT_CALL(device, EraseBlocks(48, 4096, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&deviceDone));

    Format(formatter);

    for (int request = 0; request != 9; ++request)
        CompleteDevice();

    CompleteDevice(hal::BlockDevice::Result::notPresent);
    ExpectDone(fs::Result::mediaRemoved);
}

TEST_F(FatFormatterTest, formatted_fat12_volume_parses_back)
{
    fs::SparseBlockDevice::WithStorage<16> card{ 4096 };
    fs::FatFormatter::WithStorage<8> cardFormatter{ card };

    Format(cardFormatter);
    ExpectDone();

    Sector sector;
    card.ReadBlock(0, infra::MakeRange(sector));
    EXPECT_EQ((fs::PartitionEntry{ 0x01, 13, 4083 }), fs::FirstFatPartition(infra::MakeRange(sector)));
    card.ReadBlock(13, infra::MakeRange(sector));
    auto geometry = fs::GeometryOf(*fs::ParseBootSector(infra::MakeRange(sector)), 13, 4083, 512);
    EXPECT_EQ(fs::Result::success, geometry.result);
    EXPECT_EQ(fs::FatType::fat12, geometry.geometry.type);
    EXPECT_EQ(253u, geometry.geometry.clusterCount);

    card.ReadBlock(14, infra::MakeRange(sector));
    EXPECT_EQ(0xf8, sector[0]);
    EXPECT_EQ(0xff, sector[1]);
    EXPECT_EQ(0xff, sector[2]);
    EXPECT_EQ(0x00, sector[3]);
}

TEST_F(FatFormatterTest, formatted_fat32_volume_parses_back)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;
    config.volumeId = 0xcafe;

    Format(formatter, config);
    ExpectDone();

    auto geometry = Geometry();
    EXPECT_EQ(fs::FatType::fat32, geometry.type);
    EXPECT_EQ(2u, geometry.rootCluster);
    EXPECT_EQ(0xcafeu, fs::ParseBootSector(infra::MakeRange(Read(32)))->volumeId);
}

TEST_F(FatFormatterTest, fat32_volume_has_fsinfo_and_backups)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;

    Format(formatter, config);
    ExpectDone();

    auto geometry = Geometry();
    auto info = fs::ParseFsInfo(infra::MakeRange(Read(33)), geometry.clusterCount);
    EXPECT_EQ((fs::FsInfo{ geometry.clusterCount - 1, 3 }), info);
    EXPECT_EQ(Read(32), Read(38));
    EXPECT_EQ(Read(33), Read(39));
}

TEST_F(FatFormatterTest, fat_starts_with_the_media_entry_and_the_end_of_the_root_chain)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;

    Format(formatter, config);
    ExpectDone();

    auto geometry = Geometry();

    for (uint8_t copy = 0; copy != 2; ++copy)
    {
        EXPECT_EQ(0x0ffffff8u, FatEntry(geometry, copy, 0));
        EXPECT_EQ(0x0fffffffu, FatEntry(geometry, copy, 1));
        EXPECT_EQ(0x0fffffffu, FatEntry(geometry, copy, 2));
        EXPECT_EQ(0u, FatEntry(geometry, copy, 3));
    }

    EXPECT_EQ(true, fs::CleanShutdown(fs::FatType::fat32, FatEntry(geometry, 0, 1)));
}

TEST_F(FatFormatterTest, every_fat_copy_is_cleared_and_the_root_is_zeroed)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;
    config.partitioned = false;
    auto layout = fs::ComputeFormatLayout(device.NumberOfBlocks(), 512, config);
    auto firstFat = layout->volumeStart + layout->parameters.reservedSectors;
    Junk(firstFat + 1, firstFat + 11);
    Junk(firstFat + layout->parameters.sectorsPerFat + 1, firstFat + layout->parameters.sectorsPerFat + 11);
    Junk(layout->dataStart, layout->dataStart + 1);

    Format(formatter, config);
    ExpectDone();

    auto geometry = Geometry();
    EXPECT_TRUE(IsZero(geometry.firstFatSector + 1, geometry.firstFatSector + geometry.sectorsPerFat));
    EXPECT_TRUE(IsZero(geometry.firstFatSector + geometry.sectorsPerFat + 1, geometry.firstFatSector + 2 * geometry.sectorsPerFat));
    EXPECT_TRUE(IsZero(geometry.firstDataSector, geometry.firstDataSector + 1));
}

TEST_F(FatFormatterTest, one_fat_copy_is_written_once)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;
    config.fatCopies = 1;

    Format(formatter, config);
    ExpectDone();

    EXPECT_EQ(1, Geometry().fatCopies);
}

TEST_F(FatFormatterTest, data_area_is_discarded_in_one_request_and_the_device_flushed)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;
    Junk(81000, 81010);

    Format(formatter, config);
    ExpectDone();

    EXPECT_TRUE(IsZero(81000, 81010));
    EXPECT_EQ(1u, device.EraseRequests());
    EXPECT_EQ(1u, device.FlushRequests());
}

TEST_F(FatFormatterTest, fixed_root_of_fat16_is_zeroed)
{
    fs::SparseBlockDevice::WithStorage<48> card{ 32768 };
    fs::FatFormatter::WithStorage<4> cardFormatter{ card };
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 4;
    Sector junk;
    junk.fill(0x5a);

    for (uint32_t block = 29 + 1 + 66; block != 128; ++block)
        card.WriteBlock(block, infra::MakeRange(junk));

    Format(cardFormatter, config);
    ExpectDone();

    Sector sector;

    for (uint32_t block = 29 + 1 + 66; block != 128; ++block)
    {
        card.ReadBlock(block, infra::MakeRange(sector));
        EXPECT_EQ(Sector{}, sector) << block;
    }
}

TEST_F(FatFormatterTest, unsupported_device_completes_through_the_event_dispatcher)
{
    fs::SparseBlockDevice::WithStorage<1, 4096> card{ 1024 };
    fs::FatFormatter::WithStorage<8> cardFormatter{ card };

    Format(cardFormatter);
    testing::Mock::VerifyAndClearExpectations(&done);

    ExpectDone(fs::Result::unsupported);
    EXPECT_EQ(0u, card.WriteRequests());
}

TEST_F(FatFormatterTest, formats_again)
{
    Format(formatter);
    ExpectDone();

    Format(formatter);
    ExpectDone();

    EXPECT_EQ(fs::FatType::fat12, Geometry().type);
}
