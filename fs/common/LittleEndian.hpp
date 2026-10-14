#ifndef FS_COMMON_LITTLE_ENDIAN_HPP
#define FS_COMMON_LITTLE_ENDIAN_HPP

#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    uint16_t LoadLittle16(infra::ConstByteRange bytes, std::size_t offset);
    uint32_t LoadLittle32(infra::ConstByteRange bytes, std::size_t offset);
    uint64_t LoadLittle64(infra::ConstByteRange bytes, std::size_t offset);
    void StoreLittle16(infra::ByteRange bytes, std::size_t offset, uint16_t value);
    void StoreLittle32(infra::ByteRange bytes, std::size_t offset, uint32_t value);
    void StoreLittle64(infra::ByteRange bytes, std::size_t offset, uint64_t value);
}

#endif
