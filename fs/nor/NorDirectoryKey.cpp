#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/common/Crc32.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t hashMask = 0xffffff;
        constexpr uint32_t indexBits = 8;
        constexpr uint32_t indexMask = 0xff;
    }

    uint32_t NorDirectoryKey(infra::BoundedConstString name)
    {
        return (Crc32Of(infra::StringAsByteRange(name)) & hashMask) << indexBits;
    }

    uint32_t NorLastDirectoryKey(uint32_t key)
    {
        return key | indexMask;
    }
}
