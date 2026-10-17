#include "validation/firmware/workload/WorkloadOperation.hpp"
#include <array>

namespace validation
{
    const char* OperationName(WorkloadOperation operation)
    {
        static constexpr std::array<const char*, workloadOperations> names{ "format", "mount", "unmount", "open", "read", "write", "seek", "sync", "close", "expectSize", "remove", "makeDirectory", "list", "usage", "idle" };
        return names[static_cast<std::size_t>(operation)];
    }

    bool IsPartOfAppend(WorkloadOperation operation)
    {
        return operation == WorkloadOperation::write || operation == WorkloadOperation::sync || operation == WorkloadOperation::idle;
    }
}
