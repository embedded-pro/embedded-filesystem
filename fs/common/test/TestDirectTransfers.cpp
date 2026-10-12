#include "fs/common/DirectTransfers.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MemoryRangeMatcher.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    using Sector = std::array<uint8_t, 4>;

    constexpr Sector a{ 1, 2, 3, 4 };
    constexpr Sector b{ 5, 6, 7, 8 };

    class DirectTransfersTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void CompleteDevice(hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            ASSERT_TRUE(static_cast<bool>(deviceDone));
            auto onDone = deviceDone;
            deviceDone = nullptr;
            onDone(result);
        }

        void CacheModified(uint32_t sector, uint8_t rank, const Sector& contents)
        {
            cache.Overwrite(sector, rank, [contents](fs::Result, infra::ByteRange buffer)
                {
                    infra::Copy(infra::MakeRange(contents), buffer);
                });
            ExecuteAllActions();
        }

        void ExpectFlush()
        {
            EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&deviceDone));
        }

        void ExpectDone(fs::Result result = fs::Result::success, hal::BlockDevice::Result deviceResult = hal::BlockDevice::Result::success)
        {
            EXPECT_CALL(done, callback(result));
            CompleteDevice(deviceResult);
        }

        void Write(uint32_t sector, infra::ConstByteRange data, uint8_t rank)
        {
            direct.Write(sector, data, rank, [this](fs::Result result)
                {
                    done.callback(result);
                });
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        fs::FlushBarrier barrier{ device };
        fs::SectorCache::WithStorage<3, 4> cache{ barrier };
        fs::DirectTransfers direct{ barrier, cache };
        infra::Function<void(hal::BlockDevice::Result)> deviceDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };
}

TEST_F(DirectTransfersTest, read_goes_to_the_device_and_shows_the_cached_sectors)
{
    CacheModified(11, 0, b);
    std::array<uint8_t, 8> buffer{};

    EXPECT_CALL(device, ReadBlocks(testing::_, 10, testing::_)).WillOnce([this](infra::ByteRange read, uint32_t, const infra::Function<void(hal::BlockDevice::Result)>& onDone)
        {
            std::ranges::fill(read, uint8_t{ 0xee });
            deviceDone = onDone;
        });
    direct.Read(10, infra::MakeRange(buffer), [this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone();

    EXPECT_EQ((std::array<uint8_t, 8>{ 0xee, 0xee, 0xee, 0xee, 5, 6, 7, 8 }), buffer);
}

TEST_F(DirectTransfersTest, failed_read_is_reported)
{
    std::array<uint8_t, 4> buffer{};

    EXPECT_CALL(device, ReadBlocks(testing::_, 10, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    direct.Read(10, infra::MakeRange(buffer), [this](fs::Result result)
        {
            done.callback(result);
        });

    ExpectDone(fs::Result::mediaRemoved, hal::BlockDevice::Result::notPresent);
}

TEST_F(DirectTransfersTest, write_drops_the_cached_sectors_of_its_range)
{
    CacheModified(10, 0, a);
    CacheModified(11, 0, a);
    CacheModified(12, 0, b);
    std::array<uint8_t, 8> data{};
    data.fill(9);

    EXPECT_CALL(device, WriteBlocks(infra::ContentsEqual(data), 10, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    Write(10, infra::MakeRange(data), 0);
    ExpectDone();

    EXPECT_CALL(device, WriteBlocks(infra::ContentsEqual(b), 12, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    cache.WriteBack([this](fs::Result result)
        {
            done.callback(result);
        });
    CompleteDevice();
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(DirectTransfersTest, write_above_an_unflushed_lower_rank_flushes_first)
{
    std::array<uint8_t, 4> data{};

    EXPECT_CALL(device, WriteBlocks(testing::_, 10, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    Write(10, infra::MakeRange(data), 0);
    ExpectDone();

    testing::InSequence sequence;
    ExpectFlush();
    EXPECT_CALL(device, WriteBlocks(testing::_, 20, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    Write(20, infra::MakeRange(data), 1);
    CompleteDevice();
    ExpectDone();
}

TEST_F(DirectTransfersTest, failed_write_is_reported)
{
    std::array<uint8_t, 4> data{};

    EXPECT_CALL(device, WriteBlocks(testing::_, 10, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    Write(10, infra::MakeRange(data), 0);

    ExpectDone(fs::Result::readOnly, hal::BlockDevice::Result::writeProtected);
}

TEST_F(DirectTransfersTest, erase_drops_the_cached_sectors_then_erases)
{
    CacheModified(10, 0, a);

    EXPECT_CALL(device, EraseBlocks(8, 16, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    direct.Erase(8, 16, [this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone();

    cache.WriteBack([this](fs::Result result)
        {
            done.callback(result);
        });
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(DirectTransfersTest, failed_erase_is_reported)
{
    EXPECT_CALL(device, EraseBlocks(8, 16, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
    direct.Erase(8, 16, [this](fs::Result result)
        {
            done.callback(result);
        });

    ExpectDone(fs::Result::deviceError, hal::BlockDevice::Result::failed);
}
