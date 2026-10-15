#include "fs/nor/NorGeometry.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
        constexpr uint32_t dataBlocksOfOneCall = 2;

        bool HasEqualSectors(const hal::Flash& flash)
        {
            auto sectors = flash.NumberOfSectors();
            if (sectors == 0)
                return false;

            for (uint32_t sector = 1; sector != sectors; ++sector)
                if (flash.SizeOfSector(sector) != flash.SizeOfSector(0))
                    return false;

            return true;
        }

        bool Fits(const NorGeometry& geometry)
        {
            return geometry.NodesPerBlock() != 0 && geometry.CheckpointBytes() <= geometry.PayloadSize() && geometry.blocks >= superblocks + geometry.ReserveBlocks() + geometry.WatermarkBlocks() && uint64_t{ geometry.blocks } * geometry.BlockSize() <= norModifiedNodeTag;
        }
    }

    uint32_t NorGeometry::BlockSize() const
    {
        return sectorSize * sectorsPerBlock;
    }

    uint32_t NorGeometry::HeaderPartBOffset() const
    {
        return NorHeaderPartBOffset(programUnit);
    }

    uint32_t NorGeometry::HeaderSize() const
    {
        return NorHeaderSize(programUnit);
    }

    uint32_t NorGeometry::PayloadSize() const
    {
        return BlockSize() - HeaderSize();
    }

    uint32_t NorGeometry::NodeSlotsOffset() const
    {
        return RoundUpTo(HeaderSize(), pageSize);
    }

    uint32_t NorGeometry::NodesPerBlock() const
    {
        return (BlockSize() - NodeSlotsOffset()) / nodeSize;
    }

    uint32_t NorGeometry::CheckpointBytes() const
    {
        return NorCheckpointBytes(blocks);
    }

    uint32_t NorGeometry::NodeBlocksPerCheckpoint() const
    {
        return static_cast<uint32_t>((cachedNodes + NodesPerBlock() - 1) / NodesPerBlock());
    }

    uint32_t NorGeometry::ReserveBlocks() const
    {
        return 2 * NodeBlocksPerCheckpoint() + 2;
    }

    uint32_t NorGeometry::WatermarkBlocks() const
    {
        return dataBlocksOfOneCall + 2 * NodeBlocksPerCheckpoint() + 3;
    }

    uint32_t NorGeometry::AddressOfBlock(uint32_t block) const
    {
        return block * BlockSize();
    }

    uint32_t NorGeometry::FirstSectorOfBlock(uint32_t block) const
    {
        return block * sectorsPerBlock;
    }

    uint32_t NorGeometry::RoundToProgramUnit(uint32_t size) const
    {
        return RoundUpTo(size, programUnit);
    }

    std::optional<NorGeometry> MakeNorGeometry(const NorConfig& config, uint32_t sectorSize, uint32_t sectors)
    {
        if (!IsValidNorConfig(config) || sectorSize == 0 || sectorSize % config.pageSize != 0 || sectors / config.sectorsPerBlock > config.maxBlocks)
            return std::nullopt;

        NorGeometry geometry{ sectorSize, config.sectorsPerBlock, sectors / config.sectorsPerBlock, config.programUnit, config.pageSize, config.nodeSize, config.cachedNodes };
        if (!Fits(geometry))
            return std::nullopt;

        return geometry;
    }

    std::optional<NorGeometry> MakeNorGeometry(const NorConfig& config, const hal::Flash& flash)
    {
        if (!HasEqualSectors(flash))
            return std::nullopt;

        return MakeNorGeometry(config, flash.SizeOfSector(0), flash.NumberOfSectors());
    }
}
