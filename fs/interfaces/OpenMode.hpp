#pragma once

#include <cstdint>

namespace fs
{
    enum class OpenMode : uint8_t
    {
        read,
        write,
        append,
        readWrite
    };
}
