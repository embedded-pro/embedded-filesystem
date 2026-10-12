#include "fs/common/SectorCache.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MemoryRangeMatcher.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <optional>

namespace
{
    using Sector = std::array<uint8_t, 4>;

    std::optional<Sector> ToSector(infra::ConstByteRange contents)
    {
        if (contents.size() != Sector{}.size())
            return std::nullopt;

        Sector sector{};
        infra::Copy(contents, infra::MakeRange(sector));
        return sector;
    }

    class SectorCacheTestBase
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        virtual fs::SectorCache& Cache() = 0;

        void Read(uint32_t sector)
        {
            Cache().Read(sector, [this](fs::Result result, infra::ConstByteRange contents)
                {
                    readDone.callback(result, ToSector(contents));
                });
        }

        void Modify(uint32_t sector, uint8_t rank, const Sector& contents)
        {
            Cache().Modify(sector, rank, [this, contents](fs::Result result, infra::ByteRange buffer)
                {
                    if (result == fs::Result::success)
                        infra::Copy(infra::MakeRange(contents), buffer);
                    modifyDone.callback(result);
                });
        }

        void Overwrite(uint32_t sector, uint8_t rank, const Sector& contents)
        {
            Cache().Overwrite(sector, rank, [this, contents](fs::Result result, infra::ByteRange buffer)
                {
                    overwritten = ToSector(buffer);
                    infra::Copy(infra::MakeRange(contents), buffer);
                    modifyDone.callback(result);
                });
            EXPECT_CALL(modifyDone, callback(fs::Result::success));
            ExecuteAllActions();
        }

        void WriteBack()
        {
            Cache().WriteBack([this](fs::Result result)
                {
                    writeBackDone.callback(result);
                });
        }

        void ExpectReadHit(uint32_t sector, const Sector& contents)
        {
            Read(sector);
            EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(contents)));
            ExecuteAllActions();
        }

        void ExpectWriteBackDone(fs::Result result = fs::Result::success)
        {
            EXPECT_CALL(writeBackDone, callback(result));
            ExecuteAllActions();
        }

        testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<Sector>)>> readDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> modifyDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> writeBackDone;
        std::optional<Sector> overwritten;
    };

    class SectorCacheTest
        : public SectorCacheTestBase
    {
    public:
        fs::SectorCache& Cache() override
        {
            return cache;
        }

        void ExpectRead(uint32_t sector, const Sector& contents)
        {
            EXPECT_CALL(device, ReadBlocks(testing::_, sector, testing::_)).WillOnce([this, contents](infra::ByteRange buffer, uint32_t, const infra::Function<void(hal::BlockDevice::Result)>& onDone)
                {
                    infra::Copy(infra::MakeRange(contents), buffer);
                    deviceDone = onDone;
                });
        }

        void ExpectWrite(uint32_t sector, const Sector& contents)
        {
            EXPECT_CALL(device, WriteBlocks(infra::ContentsEqual(contents), sector, testing::_)).WillOnce([this](infra::ConstByteRange, uint32_t, const infra::Function<void(hal::BlockDevice::Result)>& onDone)
                {
                    deviceDone = onDone;
                });
        }

        void CompleteDevice(hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            ASSERT_TRUE(static_cast<bool>(deviceDone));

            auto done = deviceDone;
            deviceDone = nullptr;
            done(result);
        }

        void Load(uint32_t sector, const Sector& contents)
        {
            ExpectRead(sector, contents);
            Read(sector);
            CompleteDevice();
            EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(contents)));
            ExecuteAllActions();
        }

        void ExpectWritesCompleted(const std::initializer_list<std::pair<uint32_t, Sector>>& writes)
        {
            testing::InSequence sequence;
            for (const auto& [sector, contents] : writes)
                ExpectWrite(sector, contents);

            WriteBack();
            for (std::size_t index = 0; index != writes.size(); ++index)
                CompleteDevice();

            ExpectWriteBackDone();
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        fs::SectorCache::WithStorage<3, 4> cache{ device };
        infra::Function<void(hal::BlockDevice::Result)> deviceDone;
    };

    constexpr Sector a{ 1, 2, 3, 4 };
    constexpr Sector b{ 5, 6, 7, 8 };
    constexpr Sector c{ 9, 10, 11, 12 };
    constexpr Sector d{ 13, 14, 15, 16 };
}

TEST_F(SectorCacheTest, read_miss_reads_sector_from_device)
{
    ExpectRead(7, a);
    Read(7);
    ExecuteAllActions();

    CompleteDevice();

    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(a)));
    ExecuteAllActions();
}

TEST_F(SectorCacheTest, read_hit_completes_from_event_dispatcher_without_device_request)
{
    Load(7, a);

    ExpectReadHit(7, a);
}

TEST_F(SectorCacheTest, failed_read_is_reported_and_not_cached)
{
    ExpectRead(5, a);
    Read(5);
    CompleteDevice(hal::BlockDevice::Result::crcError);
    EXPECT_CALL(readDone, callback(fs::Result::deviceError, testing::Eq(std::nullopt)));
    ExecuteAllActions();

    Load(5, a);
}

TEST_F(SectorCacheTest, not_present_reports_media_removed)
{
    ExpectRead(5, a);
    Read(5);
    CompleteDevice(hal::BlockDevice::Result::notPresent);

    EXPECT_CALL(readDone, callback(fs::Result::mediaRemoved, testing::Eq(std::nullopt)));
    ExecuteAllActions();
}

TEST_F(SectorCacheTest, full_cache_evicts_least_recently_used_clean_sector)
{
    Load(1, a);
    Load(2, b);
    Load(3, c);
    ExpectReadHit(1, a);
    Load(4, d);
    ExpectReadHit(1, a);
    ExpectReadHit(3, c);

    Load(2, b);
    ExpectReadHit(1, a);
    ExpectReadHit(3, c);
}

TEST_F(SectorCacheTest, modify_miss_reads_then_returns_writable_sector)
{
    ExpectRead(5, a);
    Modify(5, 0, b);
    CompleteDevice();
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectReadHit(5, b);
}

TEST_F(SectorCacheTest, write_back_writes_modified_sector)
{
    ExpectRead(5, a);
    Modify(5, 0, b);
    CompleteDevice();
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectWritesCompleted({ { 5, b } });

    WriteBack();
    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, write_back_without_modified_sectors_completes_without_device_request)
{
    WriteBack();

    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, overwrite_does_not_read_and_is_zero_filled)
{
    Overwrite(7, 0, a);
    EXPECT_EQ(Sector{}, overwritten);

    ExpectReadHit(7, a);
}

TEST_F(SectorCacheTest, overwrite_of_cached_sector_is_zero_filled)
{
    Load(7, a);

    Overwrite(7, 0, b);

    EXPECT_EQ(Sector{}, overwritten);
}

TEST_F(SectorCacheTest, write_back_writes_lower_ranks_first)
{
    Overwrite(100, 2, a);
    Overwrite(50, 0, b);
    Overwrite(10, 1, c);

    ExpectWritesCompleted({ { 50, b }, { 10, c }, { 100, a } });
}

TEST_F(SectorCacheTest, write_back_within_rank_is_in_ascending_sector_order)
{
    Overwrite(9, 0, a);
    Overwrite(3, 0, b);

    ExpectWritesCompleted({ { 3, b }, { 9, a } });
}

TEST_F(SectorCacheTest, write_back_issues_one_device_request_at_a_time)
{
    Overwrite(3, 0, a);
    Overwrite(9, 0, b);
    ExpectWrite(3, a);
    WriteBack();
    testing::Mock::VerifyAndClearExpectations(&device);

    ExpectWrite(9, b);
    CompleteDevice();
    CompleteDevice();

    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, write_back_stops_at_first_failure_and_keeps_sectors_modified)
{
    Overwrite(5, 0, a);
    Overwrite(6, 0, b);
    ExpectWrite(5, a);
    WriteBack();
    CompleteDevice(hal::BlockDevice::Result::failed);
    ExpectWriteBackDone(fs::Result::deviceError);

    ExpectWritesCompleted({ { 5, a }, { 6, b } });
}

TEST_F(SectorCacheTest, eviction_prefers_clean_sector_over_modified_one)
{
    Overwrite(10, 0, a);
    Load(20, b);
    Load(30, c);

    Load(40, d);

    ExpectReadHit(30, c);
    ExpectWritesCompleted({ { 10, a } });
}

TEST_F(SectorCacheTest, eviction_writes_modified_victim_before_loading)
{
    Overwrite(10, 0, a);
    Overwrite(20, 0, b);
    Overwrite(30, 0, c);

    testing::InSequence sequence;
    ExpectWrite(10, a);
    ExpectRead(40, d);
    Read(40);
    CompleteDevice();
    CompleteDevice();

    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(d)));
    ExecuteAllActions();
}

TEST_F(SectorCacheTest, eviction_chooses_lowest_modified_rank)
{
    Overwrite(10, 2, a);
    Overwrite(20, 0, b);
    Overwrite(30, 1, c);

    testing::InSequence sequence;
    ExpectWrite(20, b);
    ExpectRead(40, d);
    Read(40);
    CompleteDevice();
    CompleteDevice();

    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(d)));
    ExecuteAllActions();
}

TEST_F(SectorCacheTest, eviction_write_failure_fails_access_and_keeps_sector)
{
    Overwrite(10, 0, a);
    Overwrite(20, 0, b);
    Overwrite(30, 0, c);
    ExpectWrite(10, a);
    Read(40);
    CompleteDevice(hal::BlockDevice::Result::failed);

    EXPECT_CALL(readDone, callback(fs::Result::deviceError, testing::Eq(std::nullopt)));
    ExecuteAllActions();

    ExpectWritesCompleted({ { 10, a }, { 20, b }, { 30, c } });
}

TEST_F(SectorCacheTest, modifying_with_another_rank_writes_back_everything_first)
{
    Overwrite(5, 0, a);
    Overwrite(6, 1, b);

    {
        testing::InSequence sequence;
        ExpectWrite(5, a);
        ExpectWrite(6, b);
    }
    Modify(5, 2, c);
    CompleteDevice();
    CompleteDevice();
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectWritesCompleted({ { 5, c } });
}

TEST_F(SectorCacheTest, modifying_with_the_same_rank_writes_nothing)
{
    Overwrite(5, 1, a);
    Overwrite(6, 0, b);

    Modify(5, 1, c);
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectWritesCompleted({ { 6, b }, { 5, c } });
}

TEST_F(SectorCacheTest, mirrored_sector_is_written_to_every_copy_in_turn)
{
    cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ 32, 100, 2 });
    Overwrite(40, 1, a);

    ExpectWritesCompleted({ { 40, a }, { 140, a } });
}

TEST_F(SectorCacheTest, mirrored_victim_is_written_to_every_copy_in_turn)
{
    cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ 32, 100, 2 });
    Overwrite(40, 0, a);
    Overwrite(20, 1, b);
    Overwrite(30, 1, c);

    testing::InSequence sequence;
    ExpectWrite(40, a);
    ExpectWrite(140, a);
    ExpectRead(50, d);
    Read(50);
    CompleteDevice();
    CompleteDevice();
    CompleteDevice();

    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(d)));
    ExecuteAllActions();
}

TEST_F(SectorCacheTest, sector_outside_mirrored_region_is_written_once)
{
    cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ 32, 100, 2 });
    Overwrite(31, 0, a);
    Overwrite(132, 0, b);

    ExpectWritesCompleted({ { 31, a }, { 132, b } });
}

TEST_F(SectorCacheTest, mirroring_stops_after_clearing_the_region)
{
    cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ 32, 100, 2 });
    cache.SetMirroredRegion(std::nullopt);
    Overwrite(40, 1, a);

    ExpectWritesCompleted({ { 40, a } });
}

TEST_F(SectorCacheTest, discard_drops_cached_sectors_in_range_without_writing)
{
    Overwrite(5, 0, a);
    Overwrite(6, 0, b);
    Overwrite(7, 0, c);

    cache.Discard(5, 7);

    ExpectWritesCompleted({ { 7, c } });
    Load(5, d);
}

TEST_F(SectorCacheTest, overlay_copies_cached_sectors_into_buffer)
{
    Overwrite(5, 0, a);
    Overwrite(7, 0, c);
    Overwrite(9, 0, d);
    std::array<uint8_t, 12> buffer{};
    buffer.fill(0xee);

    cache.Overlay(5, infra::MakeRange(buffer));

    EXPECT_EQ((std::array<uint8_t, 12>{ 1, 2, 3, 4, 0xee, 0xee, 0xee, 0xee, 9, 10, 11, 12 }), buffer);
}

TEST_F(SectorCacheTest, clear_drops_every_sector)
{
    Overwrite(5, 0, a);
    Load(6, b);

    cache.Clear();

    WriteBack();
    ExpectWriteBackDone();
    Load(6, b);
}

TEST_F(SectorCacheTest, reports_its_sector_size)
{
    EXPECT_EQ(4, cache.SectorSize());
}

namespace
{
    class SectorCacheOnStubTest
        : public SectorCacheTestBase
    {
    public:
        fs::SectorCache& Cache() override
        {
            return cache;
        }

        hal::BlockDeviceStub::WithStorage<64> device{ 4 };
        fs::SectorCache::WithStorage<2, 4> cache{ device };
    };
}

TEST_F(SectorCacheOnStubTest, written_sectors_are_read_back_after_clear)
{
    Overwrite(3, 0, a);
    WriteBack();
    ExpectWriteBackDone();

    cache.Clear();
    Read(3);

    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(a)));
    ExecuteAllActions();
}
