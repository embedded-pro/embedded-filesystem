#ifndef VALIDATION_FIRMWARE_DEVICES_TEST_DOUBLES_COUNTED_DEVICE_MOCK_HPP
#define VALIDATION_FIRMWARE_DEVICES_TEST_DOUBLES_COUNTED_DEVICE_MOCK_HPP

#include "validation/firmware/devices/CountedDevice.hpp"
#include "gmock/gmock.h"

namespace validation
{
    class CountedDeviceMock
        : public CountedDevice
    {
    public:
        MOCK_METHOD(DeviceCounts, Snapshot, (), (const, override));
        MOCK_METHOD(void, StartPhase, (), (override));
        MOCK_METHOD(void, OnTimeout, (const infra::Function<void()>& onTimeout), (override));
        MOCK_METHOD(bool, InUse, (), (const, override));
    };
}

#endif
