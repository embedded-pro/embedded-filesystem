#pragma once

#include "ff.h"
#include "metrics/baselines/SynchronousDevices.hpp"
#include <cstdint>

namespace metrics
{
    struct DiskIoBinding
    {
        SynchronousBlockDevice* device{ nullptr };
        uint32_t eraseBlockSectors{ 0 };
    };

    DiskIoBinding& DiskIo();
}
