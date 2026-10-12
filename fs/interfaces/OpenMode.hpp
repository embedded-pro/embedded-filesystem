#ifndef FS_INTERFACES_OPEN_MODE_HPP
#define FS_INTERFACES_OPEN_MODE_HPP

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

#endif
