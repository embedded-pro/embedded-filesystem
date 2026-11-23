#pragma once

#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"

namespace fs
{
    Result ToResult(hal::BlockDevice::Result result);
}
