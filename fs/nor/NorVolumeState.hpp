#ifndef FS_NOR_NOR_VOLUME_STATE_HPP
#define FS_NOR_NOR_VOLUME_STATE_HPP

#include "fs/nor/NorBlockStates.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorEraseCounts.hpp"
#include "fs/nor/NorGeometry.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct NorJournal
    {
        uint32_t block{ 0 };
        uint32_t end{ 0 };
        uint32_t blocks{ 0 };

        bool operator==(const NorJournal& other) const = default;
    };

    struct NorVolumeState
    {
        NorVolumeState(infra::ByteRange blockStates, infra::ByteRange eraseCountStorage);

        std::optional<NorGeometry> geometry;
        NorBlockStates blocks;
        NorEraseCounts eraseCounts;
        NorCheckpoint checkpoint;
        uint32_t checkpointBlock{ 0 };
        NorJournal journal;
        uint64_t nextSequence{ 0 };
    };
}

#endif
