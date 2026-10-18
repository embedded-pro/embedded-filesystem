#ifndef VALIDATION_FIRMWARE_WORKLOAD_COUNT_RECORDS_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_COUNT_RECORDS_HPP

#include "validation/firmware/devices/DeviceCounts.hpp"
#include "validation/firmware/results/DeviceRecord.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"

namespace validation
{
    void AddCounts(ResultRecord& record, const DeviceCounts& counts, DeviceKind kind);
    void AddCounts(ResultRecord& record, const VolumeCounts& counts);
}

#endif
