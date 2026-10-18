#include "fs/fat/lite/FatLiteWindow.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <utility>

namespace
{
    using DeviceResult = hal::BlockDevice::Result;

    class FatLiteWindowTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Complete(const infra::Function<void(DeviceResult)>& onDone, DeviceResult result)
        {
            deviceDone = onDone;
            deviceResult = result;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    std::exchange(deviceDone, nullptr)(deviceResult);
                });
        }

        void ExpectDeviceRead(uint32_t sector, DeviceResult result = DeviceResult::success, uint8_t fill = 0)
        {
            EXPECT_CALL(device, ReadBlocks(testing::_, sector, testing::_)).WillOnce([this, result, fill](infra::ByteRange buffer, uint32_t, const infra::Function<void(DeviceResult)>& onDone)
                {
                    std::fill(buffer.begin(), buffer.end(), fill);
                    Complete(onDone, result);
                });
        }

        void ExpectDeviceWrite(uint32_t sector, uint8_t front, DeviceResult result = DeviceResult::success)
        {
            EXPECT_CALL(device, WriteBlocks(testing::_, sector, testing::_)).WillOnce([this, front, result](infra::ConstByteRange data, uint32_t, const infra::Function<void(DeviceResult)>& onDone)
                {
                    EXPECT_EQ(front, data.front());
                    Complete(onDone, result);
                });
        }

        void ExpectDeviceFlush()
        {
            EXPECT_CALL(device, Flush(testing::_)).WillOnce([this](const infra::Function<void(DeviceResult)>& onDone)
                {
                    Complete(onDone, DeviceResult::success);
                });
        }

        void Read(uint32_t sector, fs::Result expected, uint8_t front)
        {
            window.Read(sector, [this](fs::Result result, infra::ConstByteRange contents)
                {
                    readDone.callback(result, contents.front());
                });
            EXPECT_CALL(readDone, callback(expected, front));
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&readDone);
        }

        void Modify(uint32_t sector, uint8_t rank, uint8_t value, fs::Result expected = fs::Result::success)
        {
            window.Modify(sector, rank, [this, value](fs::Result result, infra::ByteRange contents)
                {
                    if (result == fs::Result::success)
                        contents.front() = value;

                    done.callback(result);
                });
            EXPECT_CALL(done, callback(expected));
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&done);
        }

        void ExpectFlush(fs::Result expected)
        {
            window.Flush([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(expected));
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&done);
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        fs::FlushBarrier barrier{ device };
        std::array<uint8_t, 512> buffer{};
        fs::FatLiteWindow window{ barrier, infra::MakeRange(buffer) };
        testing::StrictMock<infra::MockCallback<void(fs::Result, uint8_t)>> readDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
        infra::Function<void(DeviceResult)> deviceDone;
        DeviceResult deviceResult{ DeviceResult::success };
    };
}

TEST_F(FatLiteWindowTest, sector_size_is_the_size_of_the_buffer)
{
    EXPECT_EQ(512u, window.SectorSize());
}

TEST_F(FatLiteWindowTest, the_sector_it_holds_is_read_without_the_device)
{
    ExpectDeviceRead(7, DeviceResult::success, 0x5a);
    Read(7, fs::Result::success, 0x5a);

    Read(7, fs::Result::success, 0x5a);
}

TEST_F(FatLiteWindowTest, a_modified_sector_is_written_back_before_another_sector_is_read)
{
    ExpectDeviceRead(7);
    Modify(7, 0, 0x11);

    testing::InSequence sequence;
    ExpectDeviceWrite(7, 0x11);
    ExpectDeviceRead(8, DeviceResult::success, 0x22);
    Read(8, fs::Result::success, 0x22);
}

TEST_F(FatLiteWindowTest, the_held_sector_is_modified_again_without_the_device)
{
    ExpectDeviceRead(7);
    Modify(7, 1, 0x11);
    Modify(7, 1, 0x12);

    testing::InSequence sequence;
    ExpectDeviceWrite(7, 0x12);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);
}

TEST_F(FatLiteWindowTest, a_modification_with_another_rank_writes_the_sector_back_first)
{
    ExpectDeviceRead(7);
    Modify(7, 1, 0x11);

    ExpectDeviceWrite(7, 0x11);
    Modify(7, 2, 0x12);

    testing::InSequence sequence;
    ExpectDeviceFlush();
    ExpectDeviceWrite(7, 0x12);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);
}

TEST_F(FatLiteWindowTest, an_overwrite_reads_nothing_and_starts_from_zeros)
{
    buffer.fill(0x77);
    window.Overwrite(9, 0, [this](fs::Result result, infra::ByteRange contents)
        {
            EXPECT_EQ(0, contents.back());
            contents.front() = 0x33;
            done.callback(result);
        });
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();

    testing::InSequence sequence;
    ExpectDeviceWrite(9, 0x33);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);
}

TEST_F(FatLiteWindowTest, a_flush_of_an_unmodified_sector_writes_nothing)
{
    ExpectFlush(fs::Result::success);

    ExpectDeviceRead(7);
    Read(7, fs::Result::success, 0);
    ExpectFlush(fs::Result::success);
}

TEST_F(FatLiteWindowTest, a_sector_of_the_mirrored_region_is_written_to_every_copy)
{
    window.SetMirroredRegion(fs::SectorCache::MirroredRegion{ 4, 10, 2 });
    ExpectDeviceRead(13);
    Modify(13, 1, 0x44);

    testing::InSequence sequence;
    ExpectDeviceWrite(13, 0x44);
    ExpectDeviceWrite(23, 0x44);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);

    ExpectDeviceRead(14);
    Modify(14, 1, 0x45);
    ExpectDeviceWrite(14, 0x45);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);
}

TEST_F(FatLiteWindowTest, a_failed_write_back_keeps_the_sector_modified)
{
    ExpectDeviceRead(7);
    Modify(7, 0, 0x11);

    ExpectDeviceWrite(7, 0x11, DeviceResult::failed);
    ExpectFlush(fs::Result::deviceError);

    testing::InSequence sequence;
    ExpectDeviceWrite(7, 0x11);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);
}

TEST_F(FatLiteWindowTest, a_failed_write_back_before_another_sector_fails_that_access)
{
    ExpectDeviceRead(7);
    Modify(7, 0, 0x11);

    ExpectDeviceWrite(7, 0x11, DeviceResult::failed);
    window.Read(8, [this](fs::Result result, infra::ConstByteRange)
        {
            done.callback(result);
        });
    EXPECT_CALL(done, callback(fs::Result::deviceError));
    ExecuteAllActions();
    testing::Mock::VerifyAndClearExpectations(&done);

    Read(7, fs::Result::success, 0x11);
}

TEST_F(FatLiteWindowTest, a_failed_read_holds_no_sector)
{
    ExpectDeviceRead(7, DeviceResult::failed);
    Read(7, fs::Result::deviceError, 0);

    ExpectDeviceRead(7, DeviceResult::success, 0x5a);
    Read(7, fs::Result::success, 0x5a);
}

TEST_F(FatLiteWindowTest, flush_writes_back_and_then_flushes_the_device)
{
    ExpectDeviceRead(7);
    Modify(7, 2, 0x11);

    testing::InSequence sequence;
    ExpectDeviceWrite(7, 0x11);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);
}

TEST_F(FatLiteWindowTest, a_failed_write_back_fails_the_flush)
{
    ExpectDeviceRead(7);
    Modify(7, 2, 0x11);

    ExpectDeviceWrite(7, 0x11, DeviceResult::failed);
    ExpectFlush(fs::Result::deviceError);
}

TEST_F(FatLiteWindowTest, drop_forgets_a_sector_in_its_range)
{
    ExpectDeviceRead(7);
    Modify(7, 0, 0x11);

    window.Drop(8, 4);
    ExpectDeviceWrite(7, 0x11);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);

    Modify(7, 0, 0x12);
    window.Drop(5, 3);
    ExpectFlush(fs::Result::success);

    ExpectDeviceRead(7, DeviceResult::success, 0x13);
    Read(7, fs::Result::success, 0x13);
}

TEST_F(FatLiteWindowTest, overlay_copies_a_modified_sector_over_data_read_directly)
{
    std::array<uint8_t, 1536> data{};
    ExpectDeviceRead(7);
    Modify(7, 0, 0x11);

    window.Overlay(6, infra::MakeRange(data));
    EXPECT_EQ(0x11, data[512]);
    EXPECT_EQ(0, data[0]);
    EXPECT_EQ(0, data[1024]);
}

TEST_F(FatLiteWindowTest, overlay_leaves_data_alone_outside_its_range_and_after_write_back)
{
    std::array<uint8_t, 1536> data{};
    ExpectDeviceRead(7);
    Modify(7, 0, 0x11);

    window.Overlay(8, infra::MakeRange(data));
    window.Overlay(4, infra::MakeRange(data));
    EXPECT_EQ((std::array<uint8_t, 1536>{}), data);

    ExpectDeviceWrite(7, 0x11);
    ExpectDeviceFlush();
    ExpectFlush(fs::Result::success);
    window.Overlay(6, infra::MakeRange(data));
    EXPECT_EQ((std::array<uint8_t, 1536>{}), data);
}
