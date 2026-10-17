#include "fs/common/Crc32.hpp"

namespace fs
{
    uint32_t Crc32Of(infra::ConstByteRange bytes)
    {
        Crc32 crc;
        crc.Update(bytes);
        return crc.Result();
    }
}
