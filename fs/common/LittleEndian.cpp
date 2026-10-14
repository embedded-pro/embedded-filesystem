#include "fs/common/LittleEndian.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    uint16_t LoadLittle16(infra::ConstByteRange bytes, std::size_t offset)
    {
        really_assert(offset + 2 <= bytes.size());

        return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    }

    uint32_t LoadLittle32(infra::ConstByteRange bytes, std::size_t offset)
    {
        return LoadLittle16(bytes, offset) | (static_cast<uint32_t>(LoadLittle16(bytes, offset + 2)) << 16);
    }

    uint64_t LoadLittle64(infra::ConstByteRange bytes, std::size_t offset)
    {
        return LoadLittle32(bytes, offset) | (static_cast<uint64_t>(LoadLittle32(bytes, offset + 4)) << 32);
    }

    void StoreLittle16(infra::ByteRange bytes, std::size_t offset, uint16_t value)
    {
        really_assert(offset + 2 <= bytes.size());

        bytes[offset] = static_cast<uint8_t>(value);
        bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    }

    void StoreLittle32(infra::ByteRange bytes, std::size_t offset, uint32_t value)
    {
        StoreLittle16(bytes, offset, static_cast<uint16_t>(value));
        StoreLittle16(bytes, offset + 2, static_cast<uint16_t>(value >> 16));
    }

    void StoreLittle64(infra::ByteRange bytes, std::size_t offset, uint64_t value)
    {
        StoreLittle32(bytes, offset, static_cast<uint32_t>(value));
        StoreLittle32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
    }
}
