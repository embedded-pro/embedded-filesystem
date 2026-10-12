#include "fs/common/BlockDeviceResult.hpp"
#include "gtest/gtest.h"
#include <array>
#include <utility>

TEST(BlockDeviceResultTest, maps_every_block_device_result)
{
    const std::array<std::pair<hal::BlockDevice::Result, fs::Result>, 7> results{ {
        { hal::BlockDevice::Result::success, fs::Result::success },
        { hal::BlockDevice::Result::notPresent, fs::Result::mediaRemoved },
        { hal::BlockDevice::Result::timeout, fs::Result::deviceError },
        { hal::BlockDevice::Result::crcError, fs::Result::deviceError },
        { hal::BlockDevice::Result::writeProtected, fs::Result::readOnly },
        { hal::BlockDevice::Result::outOfRange, fs::Result::corrupted },
        { hal::BlockDevice::Result::failed, fs::Result::deviceError },
    } };

    for (const auto& [deviceResult, result] : results)
        EXPECT_EQ(result, fs::ToResult(deviceResult));
}
