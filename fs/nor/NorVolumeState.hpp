#ifndef FS_NOR_NOR_VOLUME_STATE_HPP
#define FS_NOR_NOR_VOLUME_STATE_HPP

#include "fs/nor/NorBlockStates.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorEraseCounts.hpp"
#include "fs/nor/NorGeometry.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorNodeBlocks.hpp"
#include "infra/util/BoundedVector.hpp"
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

    struct NorRecordPosition
    {
        uint32_t block{ 0 };
        uint32_t offset{ 0 };

        bool operator==(const NorRecordPosition& other) const = default;
    };

    struct NorVolumeState
    {
        NorVolumeState(infra::ByteRange blockStates, infra::ByteRange eraseCountStorage, infra::ByteRange nodeBlockStorage);

        std::optional<NorGeometry> geometry;
        NorBlockStates blocks;
        NorEraseCounts eraseCounts;
        NorNodeBlocks nodeBlocks;
        NorCheckpoint checkpoint;
        NorMapRoot inodeTable;
        uint32_t checkpointBlock{ 0 };
        NorJournal journal;
        infra::BoundedVector<uint32_t>::WithMaxSize<norMaxJournalBlocks> epoch;
        bool startNewJournalBlock{ false };
        uint64_t nextSequence{ 0 };
        uint32_t nextInode{ 0 };
        uint32_t liveNodes{ 0 };
        std::optional<uint32_t> nodeBlock;
        uint32_t nodeOffset{ 0 };
    };
}

#endif
