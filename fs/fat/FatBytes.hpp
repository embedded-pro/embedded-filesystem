#ifndef FS_FAT_FAT_BYTES_HPP
#define FS_FAT_FAT_BYTES_HPP

#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    uint16_t LoadLittle16(infra::ConstByteRange bytes, std::size_t offset);
    uint32_t LoadLittle32(infra::ConstByteRange bytes, std::size_t offset);
    void StoreLittle16(infra::ByteRange bytes, std::size_t offset, uint16_t value);
    void StoreLittle32(infra::ByteRange bytes, std::size_t offset, uint32_t value);
    bool HasBootSignature(infra::ConstByteRange sector);
    void StoreBootSignature(infra::ByteRange sector);
}

#endif
