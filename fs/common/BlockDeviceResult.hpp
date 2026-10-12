#ifndef FS_COMMON_BLOCK_DEVICE_RESULT_HPP
#define FS_COMMON_BLOCK_DEVICE_RESULT_HPP

#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"

namespace fs
{
    Result ToResult(hal::BlockDevice::Result result);
}

#endif
