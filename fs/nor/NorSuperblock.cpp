#include "fs/nor/NorSuperblock.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/common/Crc32.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t magic = 0x4e534645;
        constexpr uint16_t version = 1;
        constexpr std::size_t crcOffset = 24;

        uint32_t Crc(infra::ConstByteRange bytes)
        {
            return Crc32Of(infra::Head(bytes, crcOffset));
        }
    }

    std::optional<NorSuperblock> ParseNorSuperblock(infra::ConstByteRange bytes)
    {
        really_assert(bytes.size() >= norSuperblockBytes);

        if (LoadLittle32(bytes, 0) != magic || LoadLittle16(bytes, 4) != version || LoadLittle32(bytes, crcOffset) != Crc(bytes))
            return std::nullopt;

        return NorSuperblock{ LoadLittle16(bytes, 6), LoadLittle32(bytes, 8), LoadLittle32(bytes, 12), LoadLittle16(bytes, 16), LoadLittle16(bytes, 18), LoadLittle16(bytes, 20), bytes[22] };
    }

    void SerializeNorSuperblock(const NorSuperblock& superblock, infra::ByteRange bytes)
    {
        really_assert(bytes.size() >= norSuperblockBytes);

        std::ranges::fill(bytes, uint8_t{ 0xff });
        StoreLittle32(bytes, 0, magic);
        StoreLittle16(bytes, 4, version);
        StoreLittle16(bytes, 6, static_cast<uint16_t>(superblock.sectorsPerBlock));
        StoreLittle32(bytes, 8, superblock.sectorSize);
        StoreLittle32(bytes, 12, superblock.blocks);
        StoreLittle16(bytes, 16, static_cast<uint16_t>(superblock.programUnit));
        StoreLittle16(bytes, 18, static_cast<uint16_t>(superblock.nodeSize));
        StoreLittle16(bytes, 20, static_cast<uint16_t>(superblock.inlineBytes));
        bytes[22] = static_cast<uint8_t>(superblock.maxNameBytes);
        bytes[23] = 0;
        StoreLittle32(bytes, crcOffset, Crc(bytes));
    }
}
