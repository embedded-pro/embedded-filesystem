#pragma once

#include "fs/common/LittleEndian.hpp"
#include "infra/util/ByteRange.hpp"

namespace fs
{
    bool HasBootSignature(infra::ConstByteRange sector);
    void StoreBootSignature(infra::ByteRange sector);
}
