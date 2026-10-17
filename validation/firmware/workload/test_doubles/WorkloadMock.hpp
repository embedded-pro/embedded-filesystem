#ifndef VALIDATION_FIRMWARE_WORKLOAD_TEST_DOUBLES_WORKLOAD_MOCK_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_TEST_DOUBLES_WORKLOAD_MOCK_HPP

#include "validation/firmware/workload/Workload.hpp"
#include "gmock/gmock.h"

namespace validation
{
    class WorkloadMock
        : public Workload
    {
    public:
        MOCK_METHOD(infra::BoundedConstString, Name, (), (const, override));
        MOCK_METHOD(bool, Fills, (), (const, override));
        MOCK_METHOD(void, Run, (const WorkloadParameters& parameters, const infra::Function<void()>& onDone), (override));
    };
}

#endif
