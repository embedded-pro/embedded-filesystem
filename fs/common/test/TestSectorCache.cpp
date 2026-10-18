#include "fs/common/SectorCache.hpp"
#include "fs/common/TransferAlignment.hpp"
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

        void Flush()
        {
            Cache().Flush([this](fs::Result result)
                {
                    writeBackDone.callback(result);
                });
        }

        void ReadOnce(uint32_t sector)
        {
            Cache().ReadOnce(sector, [this](fs::Result result, infra::ConstByteRange contents)
                {
                    readDone.callback(result, ToSector(contents));
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

    constexpr std::pair<uint32_t, Sector> flushed{ 0xffffffff, Sector{} };

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

        template<std::size_t Count>
        void ExpectWriteRun(uint32_t sector, const std::array<Sector, Count>& run)
        {
            std::array<uint8_t, Count * Sector{}.size()> expected{};
            for (std::size_t index = 0; index != Count; ++index)
                std::copy(run[index].begin(), run[index].end(), expected.begin() + index * Sector{}.size());

            EXPECT_CALL(device, WriteBlocks(infra::ContentsEqual(expected), sector, testing::_)).WillOnce([this](infra::ConstByteRange, uint32_t, const infra::Function<void(hal::BlockDevice::Result)>& onDone)
                {
                    deviceDone = onDone;
                });
        }

        void ExpectFlush()
        {
            EXPECT_CALL(device, Flush(testing::_)).WillOnce([this](const infra::Function<void(hal::BlockDevice::Result)>& onDone)
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
                if (sector == flushed.first)
                    ExpectFlush();
                else
                    ExpectWrite(sector, contents);

            WriteBack();
            for (std::size_t index = 0; index != writes.size(); ++index)
                CompleteDevice();

            ExpectWriteBackDone();
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        fs::FlushBarrier barrier{ device };
        fs::SectorCache::WithStorage<3, 4> cache{ barrier };
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

    ExpectWritesCompleted({ { 50, b }, flushed, { 10, c }, flushed, { 100, a } });
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
    Overwrite(9, 0, b);
    ExpectWrite(5, a);
    WriteBack();
    CompleteDevice(hal::BlockDevice::Result::failed);
    ExpectWriteBackDone(fs::Result::deviceError);

    ExpectWritesCompleted({ { 5, a }, { 9, b } });
}

TEST_F(SectorCacheTest, write_back_writes_consecutive_sectors_of_one_rank_in_one_request)
{
    Overwrite(5, 0, a);
    Overwrite(6, 0, b);
    Overwrite(7, 0, c);

    ExpectWriteRun(5, std::array<Sector, 3>{ a, b, c });
    WriteBack();
    CompleteDevice();
    ExpectWriteBackDone();

    WriteBack();
    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, write_back_splits_a_run_where_the_rank_changes)
{
    Overwrite(5, 0, a);
    Overwrite(6, 1, b);

    ExpectWritesCompleted({ { 5, a }, flushed, { 6, b } });
}

TEST_F(SectorCacheTest, consecutive_sectors_in_slots_out_of_order_are_written_separately)
{
    Overwrite(6, 0, a);
    Overwrite(5, 0, b);

    ExpectWritesCompleted({ { 5, b }, { 6, a } });
}

TEST_F(SectorCacheTest, failed_run_keeps_all_its_sectors_modified)
{
    Overwrite(5, 0, a);
    Overwrite(6, 0, b);
    ExpectWriteRun(5, std::array<Sector, 2>{ a, b });
    WriteBack();
    CompleteDevice(hal::BlockDevice::Result::failed);
    ExpectWriteBackDone(fs::Result::deviceError);

    ExpectWriteRun(5, std::array<Sector, 2>{ a, b });
    WriteBack();
    CompleteDevice();
    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, write_takes_the_slot_after_the_previous_sector)
{
    Overwrite(5, 0, a);
    Load(30, c);
    Overwrite(6, 0, b);

    ExpectWriteRun(5, std::array<Sector, 2>{ a, b });
    WriteBack();
    CompleteDevice();
    ExpectWriteBackDone();

    ExpectRead(30, c);
    Read(30);
    CompleteDevice();
    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(c)));
    ExecuteAllActions();
}

TEST_F(SectorCacheTest, write_moves_a_modified_sector_out_of_the_slot_after_the_previous_sector)
{
    Overwrite(5, 0, a);
    Overwrite(40, 1, c);
    Overwrite(6, 0, b);

    testing::InSequence sequence;
    ExpectWriteRun(5, std::array<Sector, 2>{ a, b });
    ExpectFlush();
    ExpectWrite(40, c);
    WriteBack();
    for (int request = 0; request != 3; ++request)
        CompleteDevice();
    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, write_does_not_move_a_modified_sector_to_a_lower_slot)
{
    Overwrite(5, 0, a);
    Overwrite(40, 1, c);
    Overwrite(41, 1, d);
    ExpectWrite(5, a);
    Cache().Overwrite(6, 0, [this](fs::Result result, infra::ByteRange buffer)
        {
            infra::Copy(infra::MakeRange(b), buffer);
            modifyDone.callback(result);
        });
    CompleteDevice();
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    testing::InSequence sequence;
    ExpectWrite(6, b);
    ExpectFlush();
    ExpectWriteRun(40, std::array<Sector, 2>{ c, d });
    WriteBack();
    for (int request = 0; request != 3; ++request)
        CompleteDevice();
    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, write_takes_a_free_slot_before_a_clean_one)
{
    Load(30, c);
    Overwrite(9, 0, a);

    ExpectWritesCompleted({ { 9, a } });
    ExpectReadHit(30, c);
}

TEST_F(SectorCacheTest, eviction_writes_the_whole_run_around_its_victim)
{
    Overwrite(5, 0, a);
    Overwrite(6, 0, b);
    Overwrite(7, 0, c);
    Modify(5, 0, a);
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    testing::InSequence sequence;
    ExpectWriteRun(5, std::array<Sector, 3>{ a, b, c });
    ExpectRead(50, d);
    Read(50);
    CompleteDevice();
    CompleteDevice();
    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(d)));
    ExecuteAllActions();

    WriteBack();
    ExpectWriteBackDone();
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
        ExpectFlush();
        ExpectWrite(6, b);
    }
    Modify(5, 2, c);
    CompleteDevice();
    CompleteDevice();
    CompleteDevice();
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectWritesCompleted({ flushed, { 5, c } });
}

TEST_F(SectorCacheTest, modifying_with_the_same_rank_writes_nothing)
{
    Overwrite(5, 1, a);
    Overwrite(6, 0, b);

    Modify(5, 1, c);
    EXPECT_CALL(modifyDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectWritesCompleted({ { 6, b }, flushed, { 5, c } });
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

TEST_F(SectorCacheTest, discarded_sectors_are_not_part_of_a_run)
{
    Overwrite(5, 0, a);
    Overwrite(6, 0, b);
    Overwrite(7, 0, c);
    cache.Discard(5, 7);

    ExpectWritesCompleted({ { 7, c } });
}

TEST_F(SectorCacheTest, mirrored_run_is_written_once_to_every_copy)
{
    cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ 32, 100, 2 });
    Overwrite(40, 1, a);
    Overwrite(41, 1, b);

    testing::InSequence sequence;
    ExpectWriteRun(40, std::array<Sector, 2>{ a, b });
    ExpectWriteRun(140, std::array<Sector, 2>{ a, b });
    WriteBack();
    CompleteDevice();
    CompleteDevice();
    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, run_ends_at_the_end_of_the_mirrored_region)
{
    cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ 32, 100, 2 });
    Overwrite(131, 1, a);
    Overwrite(132, 1, b);

    ExpectWritesCompleted({ { 131, a }, { 231, a }, { 132, b } });
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

TEST_F(SectorCacheTest, eviction_obeys_the_flush_barrier)
{
    Overwrite(5, 0, a);
    ExpectWritesCompleted({ { 5, a } });
    Overwrite(10, 1, b);
    Overwrite(20, 1, c);
    Overwrite(30, 1, d);

    testing::InSequence sequence;
    ExpectFlush();
    ExpectWrite(10, b);
    ExpectRead(40, a);
    Read(40);
    CompleteDevice();
    CompleteDevice();
    CompleteDevice();

    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(a)));
    ExecuteAllActions();
}

TEST_F(SectorCacheTest, flush_writes_back_then_flushes_the_device)
{
    Overwrite(5, 0, a);

    testing::InSequence sequence;
    ExpectWrite(5, a);
    ExpectFlush();
    Flush();
    CompleteDevice();
    CompleteDevice();

    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, flush_without_writes_needs_no_device_request)
{
    Flush();

    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, flush_after_write_back_only_flushes_the_device)
{
    Overwrite(5, 0, a);
    ExpectWritesCompleted({ { 5, a } });

    ExpectFlush();
    Flush();
    CompleteDevice();

    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, failed_flush_is_reported_and_keeps_the_writes_unflushed)
{
    Overwrite(5, 0, a);
    ExpectWritesCompleted({ { 5, a } });

    ExpectFlush();
    Flush();
    CompleteDevice(hal::BlockDevice::Result::timeout);
    ExpectWriteBackDone(fs::Result::deviceError);

    ExpectFlush();
    Flush();
    CompleteDevice();
    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, flush_completes_from_the_event_dispatcher)
{
    Flush();
    testing::Mock::VerifyAndClearExpectations(&writeBackDone);

    ExpectWriteBackDone();
}

TEST_F(SectorCacheTest, read_once_hit_keeps_the_order_of_use)
{
    Load(10, a);
    Load(20, b);
    Load(30, c);

    ReadOnce(10);
    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(a)));
    ExecuteAllActions();

    Load(40, d);
    ExpectReadHit(20, b);
    Load(10, a);
}

TEST_F(SectorCacheTest, read_once_miss_leaves_its_slot_least_recently_used)
{
    Load(10, a);
    Load(20, b);

    ExpectRead(30, c);
    ReadOnce(30);
    CompleteDevice();
    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(c)));
    ExecuteAllActions();

    Load(40, d);
    ExpectReadHit(10, a);
    ExpectReadHit(20, b);
    Load(30, c);
}

TEST_F(SectorCacheTest, read_once_evicts_a_clean_sector)
{
    Overwrite(10, 0, a);
    Load(20, b);
    Overwrite(30, 0, c);

    ExpectRead(40, d);
    ReadOnce(40);
    CompleteDevice();
    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(d)));
    ExecuteAllActions();

    ExpectWritesCompleted({ { 10, a }, { 30, c } });
}

TEST_F(SectorCacheTest, read_once_with_only_modified_sectors_is_busy)
{
    Overwrite(10, 0, a);
    Overwrite(20, 0, b);
    Overwrite(30, 0, c);

    ReadOnce(40);
    EXPECT_CALL(readDone, callback(fs::Result::busy, testing::Eq(std::nullopt)));
    ExecuteAllActions();

    ExpectWritesCompleted({ { 10, a }, { 20, b }, { 30, c } });
}

TEST_F(SectorCacheTest, read_once_returns_a_modified_sector)
{
    Overwrite(10, 2, a);

    ReadOnce(10);
    EXPECT_CALL(readDone, callback(fs::Result::success, testing::Optional(a)));
    ExecuteAllActions();
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
        fs::FlushBarrier barrier{ device };
        fs::SectorCache::WithStorage<2, 4> cache{ barrier };
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

namespace
{
    class SectorCacheAlignmentTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        hal::BlockDeviceStub::WithStorage<3 * 512> device{ 512 };
        fs::FlushBarrier barrier{ device };
        fs::SectorCache::WithStorage<3, 512> cache{ barrier };
        fs::TransferAlignment alignment{ fs::bufferAlignment };
        testing::StrictMock<infra::MockCallback<void(fs::Result, bool)>> readDone;
    };
}

TEST_F(SectorCacheAlignmentTest, every_sector_is_aligned_for_a_driver_that_uses_dma)
{
    for (uint32_t sector = 0; sector != 3; ++sector)
    {
        cache.Read(sector, [this](fs::Result result, infra::ConstByteRange contents)
            {
                readDone.callback(result, alignment.Accepts(contents));
            });
        EXPECT_CALL(readDone, callback(fs::Result::success, true));
        ExecuteAllActions();
    }
}
