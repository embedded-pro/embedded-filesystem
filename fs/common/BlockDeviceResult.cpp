#include "fs/common/BlockDeviceResult.hpp"

namespace fs
{
    Result ToResult(hal::BlockDevice::Result result)
    {
        switch (result)
        {
            case hal::BlockDevice::Result::success:
                return Result::success;
            case hal::BlockDevice::Result::notPresent:
                return Result::mediaRemoved;
            case hal::BlockDevice::Result::writeProtected:
                return Result::readOnly;
            case hal::BlockDevice::Result::outOfRange:
                return Result::corrupted;
            default:
                return Result::deviceError;
        }
    }
}
