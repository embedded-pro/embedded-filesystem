#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/baselines/SynchronousDevices.hpp"
#include "metrics/device/MeteredBlockDevice.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::NorModel testNor{ "test-nor", 4, 1024, 256, 1, { 400us, 45ms, 50'000'000 }, { 3ms, 400ms, 50'000'000 } };
    const metrics::BlockDeviceModel testCard{ "test-card", 512, 64, 16, 2, { 100us, 25us, 50us, 0us, 10ms }, { 100us, 25us, 50us, 250ms, 10ms } };

    class SynchronousFlashTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        metrics::VirtualTime time{ systemTimerService };
        hal::FlashStub stub{ 4, 1024 };
        metrics::MeteredFlash metered{ stub, testNor };
        metrics::SynchronousFlash flash{ time, metered };
        infra::TimePoint start{ time.Now() };
    };

    class SynchronousBlockDeviceTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        metrics::VirtualTime time{ systemTimerService };
        std::vector<uint8_t> storage = std::vector<uint8_t>(64 * 512, 0xff);
        hal::BlockDeviceStub stub{ infra::MakeRange(storage), 512 };
        metrics::MeteredBlockDevice metered{ stub, testCard };
        metrics::SynchronousBlockDevice device{ time, metered };
        infra::TimePoint start{ time.Now() };
        std::array<uint8_t, 512> block{};
    };
}

TEST_F(SynchronousFlashTest, reports_the_geometry)
{
    EXPECT_EQ(1024, flash.SectorSize());
    EXPECT_EQ(4, flash.NumberOfSectors());
}

TEST_F(SynchronousFlashTest, write_returns_once_the_device_completed)
{
    std::array<uint8_t, 2> data{ 1, 2 };

    EXPECT_TRUE(flash.Write(infra::MakeRange(data), 1030));

    EXPECT_EQ(start + 400us, time.Now());
    EXPECT_EQ(1, stub.sectors[1][6]);
    EXPECT_EQ(2, stub.sectors[1][7]);
}

TEST_F(SynchronousFlashTest, read_returns_the_stored_data)
{
    stub.sectors[2][0] = 7;
    std::array<uint8_t, 1> data{};

    EXPECT_TRUE(flash.Read(infra::MakeRange(data), 2048));

    EXPECT_EQ(7, data[0]);
    EXPECT_LT(start, time.Now());
}

TEST_F(SynchronousFlashTest, erase_sector_erases_one_sector)
{
    stub.sectors[1][0] = 0;
    stub.sectors[2][0] = 0;

    EXPECT_TRUE(flash.EraseSector(1));

    EXPECT_EQ(start + 45ms, time.Now());
    EXPECT_EQ(0xff, stub.sectors[1][0]);
    EXPECT_EQ(0, stub.sectors[2][0]);
}

TEST(SynchronousFlashWithoutCompletionTest, a_request_that_never_completes_fails)
{
    infra::ClockFixture clock;
    metrics::VirtualTime time{ clock.systemTimerService };
    testing::StrictMock<hal::FlashMock> mock;
    metrics::SynchronousFlash flash{ time, mock };
    std::array<uint8_t, 2> data{};

    EXPECT_CALL(mock, writeBufferMock(testing::_, 0));

    EXPECT_FALSE(flash.Write(infra::MakeRange(data), 0));
}

TEST_F(SynchronousBlockDeviceTest, reports_the_geometry)
{
    EXPECT_EQ(512, device.BlockSize());
    EXPECT_EQ(64, device.NumberOfBlocks());
}

TEST_F(SynchronousBlockDeviceTest, write_returns_the_result_once_the_device_completed)
{
    block.fill(0x5a);

    EXPECT_EQ(hal::BlockDevice::Result::success, device.Write(infra::MakeRange(block), 3));

    EXPECT_EQ(start + 100us + 50us, time.Now());
    EXPECT_EQ(0x5a, storage[3 * 512]);
}

TEST_F(SynchronousBlockDeviceTest, read_returns_the_stored_blocks)
{
    storage[512] = 9;

    EXPECT_EQ(hal::BlockDevice::Result::success, device.Read(infra::MakeRange(block), 1));

    EXPECT_EQ(9, block[0]);
}

TEST_F(SynchronousBlockDeviceTest, discard_and_flush_complete)
{
    EXPECT_EQ(hal::BlockDevice::Result::success, device.Discard(0, 4));
    EXPECT_EQ(hal::BlockDevice::Result::success, device.Flush());

    EXPECT_EQ(1, metered.Counters().discardRequests);
    EXPECT_EQ(1, metered.Counters().flushRequests);
}

TEST_F(SynchronousBlockDeviceTest, a_failure_of_the_device_is_returned)
{
    stub.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    EXPECT_EQ(hal::BlockDevice::Result::crcError, device.Read(infra::MakeRange(block), 0));
}

TEST(SynchronousBlockDeviceWithoutCompletionTest, a_request_that_never_completes_times_out)
{
    infra::ClockFixture clock;
    metrics::VirtualTime time{ clock.systemTimerService };
    testing::StrictMock<hal::BlockDeviceMock> mock;
    metrics::SynchronousBlockDevice device{ time, mock };

    EXPECT_CALL(mock, Flush(testing::_));

    EXPECT_EQ(hal::BlockDevice::Result::timeout, device.Flush());
}
