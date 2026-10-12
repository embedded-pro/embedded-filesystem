#ifndef METRICS_BASELINES_FATFS_DISK_IO_HPP
#define METRICS_BASELINES_FATFS_DISK_IO_HPP

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

#endif
