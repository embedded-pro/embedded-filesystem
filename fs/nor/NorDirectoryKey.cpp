#include "fs/nor/NorDirectoryKey.hpp"
#include "infra/util/Crc.hpp"

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
        infra::Crc32 crc;
        crc.Update(infra::StringAsByteRange(name));
        return (crc.Result() & hashMask) << indexBits;
    }

    uint32_t NorLastDirectoryKey(uint32_t key)
    {
        return key | indexMask;
    }
}
