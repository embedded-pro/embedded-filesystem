#pragma once

#include <cstddef>
#include <cstdint>

namespace validation
{
    enum class WorkloadOperation : uint8_t
    {
        format,
        mount,
        unmount,
        open,
        read,
        write,
        seek,
        sync,
        close,
        expectSize,
        remove,
        makeDirectory,
        list,
        usage,
        idle
    };

    constexpr std::size_t workloadOperations = 15;

    const char* OperationName(WorkloadOperation operation);
    bool IsPartOfAppend(WorkloadOperation operation);
}
