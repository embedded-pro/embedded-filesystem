#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "validation/firmware/devices/NullBlockDevice.hpp"
#include "validation/firmware/devices/NullFlash.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>

class NullFlashTest
    : public testing::Test
    , public infra::EventDispatcherFixture
{
public:
    validation::NullFlash flash{ 4096, 4096 };
    testing::StrictMock<infra::MockCallback<void()>> done;
};

TEST_F(NullFlashTest, has_the_given_geometry)
{
    EXPECT_EQ(4096, flash.NumberOfSectors());
    EXPECT_EQ(4096, flash.SizeOfSector(0));
}

TEST_F(NullFlashTest, a_read_gives_erased_bytes_from_the_event_dispatcher)
{
    std::array<uint8_t, 4> buffer{ { 1, 2, 3, 4 } };

    flash.ReadBuffer(infra::MakeRange(buffer), 0, [this]
        {
            done.callback();
        });

    EXPECT_CALL(done, callback());
    ExecuteAllActions();
    EXPECT_EQ((std::array<uint8_t, 4>{ { 0xff, 0xff, 0xff, 0xff } }), buffer);
}

TEST_F(NullFlashTest, a_write_completes_from_the_event_dispatcher)
{
    const std::array<uint8_t, 2> data{ { 1, 2 } };

    flash.WriteBuffer(infra::MakeRange(data), 0, [this]
        {
            done.callback();
        });

    EXPECT_CALL(done, callback());
    ExecuteAllActions();
}

TEST_F(NullFlashTest, an_erase_completes_from_the_event_dispatcher)
{
    flash.EraseSectors(0, 2, [this]
        {
            done.callback();
        });

    EXPECT_CALL(done, callback());
    ExecuteAllActions();
}

class NullBlockDeviceTest
    : public testing::Test
    , public infra::EventDispatcherFixture
{
public:
    validation::NullBlockDevice device{ 524288 };
    testing::StrictMock<infra::MockCallback<void(hal::BlockDevice::Result)>> done;

    infra::Function<void(hal::BlockDevice::Result)> Done()
    {
        return [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        };
    }
};

TEST_F(NullBlockDeviceTest, has_blocks_of_512_bytes)
{
    EXPECT_EQ(512, device.BlockSize());
    EXPECT_EQ(524288, device.NumberOfBlocks());
}

TEST_F(NullBlockDeviceTest, a_read_gives_zeros_from_the_event_dispatcher)
{
    std::array<uint8_t, 4> buffer{ { 1, 2, 3, 4 } };

    device.ReadBlocks(infra::MakeRange(buffer), 0, Done());

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    ExecuteAllActions();
    EXPECT_EQ((std::array<uint8_t, 4>{}), buffer);
}

TEST_F(NullBlockDeviceTest, a_write_completes_from_the_event_dispatcher)
{
    const std::array<uint8_t, 2> data{ { 1, 2 } };

    device.WriteBlocks(infra::MakeRange(data), 0, Done());

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    ExecuteAllActions();
}

TEST_F(NullBlockDeviceTest, an_erase_completes_from_the_event_dispatcher)
{
    device.EraseBlocks(0, 8, Done());

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    ExecuteAllActions();
}

TEST_F(NullBlockDeviceTest, a_flush_completes_from_the_event_dispatcher)
{
    device.Flush(Done());

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    ExecuteAllActions();
}
