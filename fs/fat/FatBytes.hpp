#ifndef FS_FAT_FAT_BYTES_HPP
#define FS_FAT_FAT_BYTES_HPP

#include "fs/common/LittleEndian.hpp"
#include "infra/util/ByteRange.hpp"

namespace fs
{
    bool HasBootSignature(infra::ConstByteRange sector);
    void StoreBootSignature(infra::ByteRange sector);
}

#endif
