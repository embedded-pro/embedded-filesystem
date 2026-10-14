#ifndef FS_NOR_NOR_CHECKPOINT_HPP
#define FS_NOR_NOR_CHECKPOINT_HPP

#include "fs/nor/NorLayout.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct NorCheckpoint
    {
        uint32_t inodeTableRoot{ 0 };
        uint8_t inodeTableHeight{ 0 };
        uint32_t nextInode{ 0 };
        uint32_t recordsBlock{ 0 };
        uint32_t recordsOffset{ 0 };
        uint32_t liveNodes{ 0 };
        uint32_t blocks{ 0 };

        bool operator==(const NorCheckpoint& other) const = default;
    };

    void SerializeNorCheckpoint(const NorCheckpoint& checkpoint, infra::ByteRange bytes);
    void MarkNorBlockInUse(infra::ByteRange checkpoint, uint32_t block);
    void SealNorCheckpoint(infra::ByteRange checkpoint);
    std::optional<NorCheckpoint> OpenNorCheckpoint(infra::ConstByteRange bytes);
    bool IsNorBlockInUse(infra::ConstByteRange checkpoint, uint32_t block);
}

#endif
