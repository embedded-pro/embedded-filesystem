#ifndef FS_NOR_NOR_SUPERBLOCK_HPP
#define FS_NOR_NOR_SUPERBLOCK_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct NorSuperblock
    {
        uint32_t sectorsPerBlock;
        uint32_t sectorSize;
        uint32_t blocks;
        uint32_t programUnit;
        uint32_t nodeSize;
        uint32_t inlineBytes;
        uint32_t maxNameBytes;

        bool operator==(const NorSuperblock& other) const = default;
    };

    std::optional<NorSuperblock> ParseNorSuperblock(infra::ConstByteRange bytes);
    void SerializeNorSuperblock(const NorSuperblock& superblock, infra::ByteRange bytes);
}

#endif
