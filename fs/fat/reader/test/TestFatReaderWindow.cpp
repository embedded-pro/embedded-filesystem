#include "fs/fat/reader/FatReaderWindow.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    class FatReaderWindowTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void ExpectDeviceRead(uint32_t sector, hal::BlockDevice::Result result, uint8_t fill = 0)
        {
            EXPECT_CALL(device, ReadBlocks(testing::_, sector, testing::_)).WillOnce([this, result, fill](infra::ByteRange buffer, uint32_t, const infra::Function<void(hal::BlockDevice::Result)>& onDone)
                {
                    std::fill(buffer.begin(), buffer.end(), fill);
                    deviceDone = onDone;
                    deviceResult = result;
                });
        }

        void CompleteDeviceRead()
        {
            deviceDone(deviceResult);
        }

        void Read(uint32_t sector)
        {
            window.Read(sector, [this](fs::Result result, infra::ConstByteRange contents)
                {
                    readDone.callback(result, contents.size(), contents.empty() ? 0 : contents.front());
                });
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        std::array<uint8_t, 512> buffer{};
        fs::FatReaderWindow window{ device, infra::MakeRange(buffer) };
        testing::StrictMock<infra::MockCallback<void(fs::Result, std::size_t, uint8_t)>> readDone;
        infra::Function<void(hal::BlockDevice::Result)> deviceDone;
        hal::BlockDevice::Result deviceResult{ hal::BlockDevice::Result::success };
    };
}

TEST_F(FatReaderWindowTest, sector_size_is_the_size_of_the_buffer)
{
    EXPECT_EQ(512u, window.SectorSize());
}

TEST_F(FatReaderWindowTest, a_sector_is_read_from_the_device)
{
    ExpectDeviceRead(7, hal::BlockDevice::Result::success, 0x5a);
    Read(7);

    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x5a));
    CompleteDeviceRead();
}

TEST_F(FatReaderWindowTest, the_sector_it_holds_is_delivered_by_the_event_dispatcher_without_the_device)
{
    ExpectDeviceRead(7, hal::BlockDevice::Result::success, 0x5a);
    Read(7);
    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x5a));
    CompleteDeviceRead();

    Read(7);
    testing::Mock::VerifyAndClearExpectations(&readDone);
    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x5a));
    ExecuteAllActions();
}

TEST_F(FatReaderWindowTest, another_sector_replaces_the_one_it_holds)
{
    ExpectDeviceRead(7, hal::BlockDevice::Result::success, 0x5a);
    Read(7);
    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x5a));
    CompleteDeviceRead();

    ExpectDeviceRead(8, hal::BlockDevice::Result::success, 0x11);
    Read(8);
    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x11));
    CompleteDeviceRead();

    ExpectDeviceRead(7, hal::BlockDevice::Result::success, 0x5a);
    Read(7);
    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x5a));
    CompleteDeviceRead();
}

TEST_F(FatReaderWindowTest, a_failed_read_holds_no_sector)
{
    ExpectDeviceRead(7, hal::BlockDevice::Result::crcError);
    Read(7);
    EXPECT_CALL(readDone, callback(fs::Result::deviceError, 512, 0));
    CompleteDeviceRead();

    ExpectDeviceRead(7, hal::BlockDevice::Result::notPresent);
    Read(7);
    EXPECT_CALL(readDone, callback(fs::Result::mediaRemoved, 512, 0));
    CompleteDeviceRead();
}

TEST_F(FatReaderWindowTest, forget_reads_the_sector_again)
{
    ExpectDeviceRead(7, hal::BlockDevice::Result::success, 0x5a);
    Read(7);
    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x5a));
    CompleteDeviceRead();

    window.Forget();

    ExpectDeviceRead(7, hal::BlockDevice::Result::success, 0x66);
    Read(7);
    EXPECT_CALL(readDone, callback(fs::Result::success, 512, 0x66));
    CompleteDeviceRead();
}
