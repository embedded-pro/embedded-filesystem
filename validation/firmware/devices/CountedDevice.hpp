#ifndef VALIDATION_FIRMWARE_DEVICES_COUNTED_DEVICE_HPP
#define VALIDATION_FIRMWARE_DEVICES_COUNTED_DEVICE_HPP

#include "infra/util/Function.hpp"
#include "validation/firmware/devices/DeviceCounts.hpp"

namespace validation
{
    // What a workload needs of a counting device: its counts, the start of a phase, the action when a request outlasts its
    // guard, and whether a request is pending or watched
    class CountedDevice
    {
    protected:
        CountedDevice() = default;
        CountedDevice(const CountedDevice& other) = delete;
        CountedDevice& operator=(const CountedDevice& other) = delete;
        ~CountedDevice() = default;

    public:
        virtual DeviceCounts Snapshot() const = 0;
        virtual void StartPhase() = 0;
        virtual void OnTimeout(const infra::Function<void()>& onTimeout) = 0;
        virtual bool InUse() const = 0;
    };
}

#endif
