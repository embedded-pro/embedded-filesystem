#ifndef FS_NOR_NOR_GEOMETRY_HPP
#define FS_NOR_NOR_GEOMETRY_HPP

#include "fs/nor/NorConfig.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    struct NorGeometry
    {
        uint32_t sectorSize;
        uint32_t sectorsPerBlock;
        uint32_t blocks;
        uint32_t programUnit;
        uint32_t pageSize;
        uint32_t nodeSize;
        std::size_t cachedNodes;

        uint32_t BlockSize() const;
        uint32_t HeaderPartBOffset() const;
        uint32_t HeaderSize() const;
        uint32_t PayloadSize() const;
        uint32_t NodeSlotsOffset() const;
        uint32_t NodesPerBlock() const;
        uint32_t CheckpointBytes() const;
        uint32_t NodeBlocksPerCheckpoint() const;
        uint32_t ReserveBlocks() const;
        uint32_t WatermarkBlocks() const;
        uint32_t AddressOfBlock(uint32_t block) const;
        uint32_t FirstSectorOfBlock(uint32_t block) const;
        uint32_t RoundToProgramUnit(uint32_t size) const;
    };

    std::optional<NorGeometry> MakeNorGeometry(const NorConfig& config, uint32_t sectorSize, uint32_t sectors);
}

#endif
