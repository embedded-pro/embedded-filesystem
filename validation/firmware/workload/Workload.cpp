#include "validation/firmware/workload/Workload.hpp"
#include "infra/stream/StringOutputStream.hpp"

namespace validation
{
    PhaseName MakePhase(infra::BoundedConstString words, uint32_t value)
    {
        PhaseName phase;
        infra::StringOutputStream stream{ phase };
        stream << words << "," << value;
        return phase;
    }

    PhaseName MakePhase(infra::BoundedConstString words, uint32_t first, uint32_t second)
    {
        PhaseName phase;
        infra::StringOutputStream stream{ phase };
        stream << words << "," << first << "," << second;
        return phase;
    }

    WorkloadPath MakePath(infra::BoundedConstString directory, uint32_t index)
    {
        WorkloadPath path;
        infra::StringOutputStream stream{ path };
        stream << directory << "/" << index;
        return path;
    }
}
