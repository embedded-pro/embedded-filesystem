#ifndef VALIDATION_FIRMWARE_WORKLOAD_TEST_DOUBLES_WORKLOAD_VOLUME_MOCK_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_TEST_DOUBLES_WORKLOAD_VOLUME_MOCK_HPP

#include "validation/firmware/workload/WorkloadVolume.hpp"
#include "gmock/gmock.h"

namespace validation
{
    class WorkloadVolumeMock
        : public WorkloadVolume
    {
    public:
        MOCK_METHOD(infra::BoundedConstString, Subject, (), (const, override));
        MOCK_METHOD(std::size_t, RamBytes, (), (const, override));
        MOCK_METHOD(fs::FileSystem&, FileSystem, (), (override));
        MOCK_METHOD(void, Format, (const infra::Function<void(fs::Result result)>& onDone), (override));
        MOCK_METHOD(std::optional<VolumeCounts>, Counters, (), (const, override));
    };
}

#endif
