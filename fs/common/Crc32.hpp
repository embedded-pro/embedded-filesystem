#ifndef FS_COMMON_CRC32_HPP
#define FS_COMMON_CRC32_HPP

#include "infra/util/ByteRange.hpp"
#include "infra/util/Crc.hpp"
#include <cstdint>

namespace fs
{
    using Crc32 = infra::Crc32Nibblewise;

    uint32_t Crc32Of(infra::ConstByteRange bytes);
}

#endif
