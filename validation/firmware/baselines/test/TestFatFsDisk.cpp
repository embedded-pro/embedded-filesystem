#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "validation/firmware/baselines/FatFsDisk.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <tuple>
// ff.h includes windows.h on Windows, so it follows the other headers
#include "ff.h"
// diskio.h uses the types that ff.h defines
#include "diskio.h"

class FatFsDiskTest
    : public testing::Test
    , public infra::EventDispatcherFixture
{
public:
    FatFsDiskTest()
    {
        EXPECT_CALL(device, BlockSize()).WillRepeatedly(testing::Return(512));
    }

    auto Completes(hal::BlockDevice::Result result)
    {
        return [this, result](auto&&... arguments)
        {
            pending = std::get<sizeof...(arguments) - 1>(std::forward_as_tuple(arguments...));
            infra::EventDispatcher::Instance().Schedule([this, result]
                {
                    completed = true;
                    pending(result);
                });
        };
    }

    testing::StrictMock<hal::BlockDeviceMock> device;
    validation::FatFsDisk disk{ device, 8 };
    infra::Function<void(hal::BlockDevice::Result)> pending;
    bool completed = false;
};

TEST(FatFsDiskWithoutDeviceTest, a_drive_without_a_disk_is_not_initialized)
{
    EXPECT_EQ(STA_NOINIT, disk_initialize(0));
    EXPECT_EQ(STA_NOINIT, disk_status(0));
}

TEST_F(FatFsDiskTest, a_drive_with_a_disk_is_ready)
{
    EXPECT_EQ(0, disk_initialize(0));
    EXPECT_EQ(0, disk_status(0));
}

TEST_F(FatFsDiskTest, a_read_waits_for_the_device)
{
    std::array<BYTE, 1024> buffer{};

    EXPECT_CALL(device, ReadBlocks(infra::ByteRange(buffer.data(), buffer.data() + 1024), 7, testing::_)).WillOnce(Completes(hal::BlockDevice::Result::success));
    EXPECT_EQ(RES_OK, disk_read(0, buffer.data(), 7, 2));
    EXPECT_TRUE(completed);
}

TEST_F(FatFsDiskTest, a_failed_read_is_an_error)
{
    std::array<BYTE, 512> buffer{};

    EXPECT_CALL(device, ReadBlocks(testing::_, 7, testing::_)).WillOnce(Completes(hal::BlockDevice::Result::crcError));
    EXPECT_EQ(RES_ERROR, disk_read(0, buffer.data(), 7, 1));
}

TEST_F(FatFsDiskTest, a_write_waits_for_the_device)
{
    const std::array<BYTE, 512> buffer{};

    EXPECT_CALL(device, WriteBlocks(infra::ConstByteRange(buffer.data(), buffer.data() + 512), 3, testing::_)).WillOnce(Completes(hal::BlockDevice::Result::success));
    EXPECT_EQ(RES_OK, disk_write(0, buffer.data(), 3, 1));
    EXPECT_TRUE(completed);
}

TEST_F(FatFsDiskTest, a_failed_write_is_an_error)
{
    const std::array<BYTE, 512> buffer{};

    EXPECT_CALL(device, WriteBlocks(testing::_, 3, testing::_)).WillOnce(Completes(hal::BlockDevice::Result::failed));
    EXPECT_EQ(RES_ERROR, disk_write(0, buffer.data(), 3, 1));
}

TEST_F(FatFsDiskTest, a_sync_flushes_the_device)
{
    EXPECT_CALL(device, Flush(testing::_)).WillOnce(Completes(hal::BlockDevice::Result::success));
    EXPECT_EQ(RES_OK, disk_ioctl(0, CTRL_SYNC, nullptr));
    EXPECT_TRUE(completed);

    EXPECT_CALL(device, Flush(testing::_)).WillOnce(Completes(hal::BlockDevice::Result::timeout));
    EXPECT_EQ(RES_ERROR, disk_ioctl(0, CTRL_SYNC, nullptr));
}

TEST_F(FatFsDiskTest, the_geometry_comes_from_the_device_and_the_erase_block)
{
    LBA_t sectors = 0;
    WORD sectorSize = 0;
    DWORD blockSize = 0;

    EXPECT_CALL(device, NumberOfBlocks()).WillOnce(testing::Return(524288));
    EXPECT_EQ(RES_OK, disk_ioctl(0, GET_SECTOR_COUNT, &sectors));
    EXPECT_EQ(RES_OK, disk_ioctl(0, GET_SECTOR_SIZE, &sectorSize));
    EXPECT_EQ(RES_OK, disk_ioctl(0, GET_BLOCK_SIZE, &blockSize));

    EXPECT_EQ(524288, sectors);
    EXPECT_EQ(512, sectorSize);
    EXPECT_EQ(8, blockSize);
}

TEST_F(FatFsDiskTest, a_trim_erases_the_blocks_up_to_and_including_the_last)
{
    std::array<LBA_t, 2> range{ { 16, 23 } };

    EXPECT_CALL(device, EraseBlocks(16, 24, testing::_)).WillOnce(Completes(hal::BlockDevice::Result::success));
    EXPECT_EQ(RES_OK, disk_ioctl(0, CTRL_TRIM, range.data()));
    EXPECT_TRUE(completed);
}

TEST_F(FatFsDiskTest, an_unknown_command_is_a_parameter_error)
{
    EXPECT_EQ(RES_PARERR, disk_ioctl(0, 99, nullptr));
}

TEST(FatFsDiskWithoutEraseBlockTest, an_unknown_erase_block_is_a_parameter_error)
{
    testing::StrictMock<hal::BlockDeviceMock> device;
    validation::FatFsDisk disk{ device, 0 };
    DWORD blockSize = 0;

    EXPECT_EQ(RES_PARERR, disk_ioctl(0, GET_BLOCK_SIZE, &blockSize));
}
