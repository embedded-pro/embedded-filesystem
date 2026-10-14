#ifndef FS_NOR_NOR_SUPERBLOCK_HPP
#define FS_NOR_NOR_SUPERBLOCK_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct NorSuperblock
    {
        uint32_t sectorsPerBlock{ 0 };
        uint32_t sectorSize{ 0 };
        uint32_t blocks{ 0 };
        uint32_t programUnit{ 0 };
        uint32_t nodeSize{ 0 };
        uint32_t inlineBytes{ 0 };
        uint32_t maxNameBytes{ 0 };

        bool operator==(const NorSuperblock& other) const = default;
    };

    std::optional<NorSuperblock> ParseNorSuperblock(infra::ConstByteRange bytes);
    void SerializeNorSuperblock(const NorSuperblock& superblock, infra::ByteRange bytes);
}

#endif
