#pragma once

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
