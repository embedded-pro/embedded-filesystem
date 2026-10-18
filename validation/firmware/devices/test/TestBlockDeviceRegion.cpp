#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "validation/firmware/devices/BlockDeviceRegion.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>

namespace
{
    class BlockDeviceRegionTest
        : public testing::Test
    {
    public:
        infra::Function<void(hal::BlockDevice::Result)> Done()
        {
            return [this](hal::BlockDevice::Result result)
            {
                done.callback(result);
            };
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        validation::BlockDeviceRegion region{ device, 64, 128 };
        testing::StrictMock<infra::MockCallback<void(hal::BlockDevice::Result)>> done;
        infra::Function<void(hal::BlockDevice::Result)> pending;
        std::array<uint8_t, 1024> buffer{};
    };
}

TEST_F(BlockDeviceRegionTest, has_the_blocks_of_the_region_that_the_device_holds)
{
    EXPECT_CALL(device, BlockSize()).WillOnce(testing::Return(512));
    EXPECT_CALL(device, NumberOfBlocks()).WillOnce(testing::Return(1000)).WillOnce(testing::Return(100)).WillOnce(testing::Return(10));

    EXPECT_EQ(512, region.BlockSize());
    EXPECT_EQ(128, region.NumberOfBlocks());
    EXPECT_EQ(36, region.NumberOfBlocks());
    EXPECT_EQ(0, region.NumberOfBlocks());
}

TEST_F(BlockDeviceRegionTest, reads_and_writes_from_the_start_of_the_region)
{
    EXPECT_CALL(device, ReadBlocks(testing::_, 64 + 3, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    region.ReadBlocks(infra::MakeRange(buffer), 3, Done());
    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    pending(hal::BlockDevice::Result::success);

    EXPECT_CALL(device, WriteBlocks(testing::_, 64 + 7, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    region.WriteBlocks(infra::MakeRange(buffer), 7, Done());
    EXPECT_CALL(done, callback(hal::BlockDevice::Result::crcError));
    pending(hal::BlockDevice::Result::crcError);
}

TEST_F(BlockDeviceRegionTest, discards_and_flushes_through_the_device)
{
    EXPECT_CALL(device, EraseBlocks(64 + 8, 64 + 16, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    region.EraseBlocks(8, 16, Done());
    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    pending(hal::BlockDevice::Result::success);

    EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&pending));
    region.Flush(Done());
    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    pending(hal::BlockDevice::Result::success);
}
