#include "fs/nor/NorFormatter.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorSuperblock.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
        constexpr uint32_t nodeBlock = 2;
        constexpr uint32_t checkpointBlock = 3;
        constexpr uint64_t nodeBlockSequence = 1;
        constexpr uint64_t checkpointSequence = 2;
        constexpr uint32_t rootInode = 1;
    }

    NorFormatter::NorFormatter(hal::Flash& flash, const NorConfig& config, infra::ByteRange buffer)
        : flash(flash)
        , config(config)
        , buffer(buffer)
    {
        really_assert(buffer.size() >= BufferBytes(config));
    }

    NorFormatter::~NorFormatter() = default;

    void NorFormatter::Format(const infra::Function<void(Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->onDone = onDone;
        geometry = HasEqualSectors() ? MakeNorGeometry(config, flash.SizeOfSector(0), flash.NumberOfSectors()) : std::nullopt;
        if (!geometry.has_value())
        {
            Complete(Result::unsupported);
            return;
        }

        eraseCountSum = 0;
        eraseCountsFound = 0;
        block = superblocks;
        ReadEraseCount();
    }

    void NorFormatter::ReadEraseCount()
    {
        ReadHeader(block, [this]
            {
                auto count = ParseNorBlockHeader(buffer, geometry->programUnit).eraseCount;
                if (count.has_value())
                {
                    eraseCountSum += *count;
                    ++eraseCountsFound;
                }

                if (++block != geometry->blocks)
                    ReadEraseCount();
                else
                    EraseSuperblocks();
            });
    }

    void NorFormatter::EraseSuperblocks()
    {
        EraseSectors(0, geometry->FirstSectorOfBlock(superblocks), [this]
            {
                block = superblocks;
                FormatBlock();
            });
    }

    void NorFormatter::FormatBlock()
    {
        ReadHeader(block, [this]
            {
                eraseCount = ParseNorBlockHeader(buffer, geometry->programUnit).eraseCount.value_or(AverageEraseCount()) + 1;
                EraseSectors(geometry->FirstSectorOfBlock(block), geometry->FirstSectorOfBlock(block + 1), [this]
                    {
                        ProgramPartA();
                    });
            });
    }

    void NorFormatter::ProgramPartA()
    {
        auto partA = Rounded(norHeaderPartABytes);
        SerializeNorHeaderA(eraseCount, partA);
        Program(partA, geometry->AddressOfBlock(block), [this]
            {
                if (++block != geometry->blocks)
                    FormatBlock();
                else
                    WriteNodeAllocation();
            });
    }

    void NorFormatter::WriteNodeAllocation()
    {
        auto partB = Rounded(norHeaderPartBBytes);
        SerializeNorHeaderB(NorAllocation{ NorBlockType::node, nodeBlockSequence }, partB);
        Program(partB, geometry->AddressOfBlock(nodeBlock) + geometry->HeaderPartBOffset(), [this]
            {
                WriteRootNode();
            });
    }

    void NorFormatter::WriteRootNode()
    {
        auto node = infra::Head(buffer, config.nodeSize);
        auto entries = NorNodeEntries(node);
        std::ranges::fill(entries, uint8_t{ 0 });
        StoreLittle32(entries, 0, rootInode);
        SerializeNorInodeRecord(NorInodeRecord{ 0, NorInodeType::directory }, infra::Head(infra::DiscardHead(entries, 4), NorInodeRecordBytes(config)), config);
        SealNorNode(NorNodeHeader{ NorMap::inodeTable, 0, 1, 0, rootInode }, node);
        Program(node, geometry->AddressOfBlock(nodeBlock) + geometry->NodeSlotsOffset(), [this]
            {
                WriteCheckpointAllocation();
            });
    }

    void NorFormatter::WriteCheckpointAllocation()
    {
        auto partB = Rounded(norHeaderPartBBytes);
        SerializeNorHeaderB(NorAllocation{ NorBlockType::checkpoint, checkpointSequence }, partB);
        Program(partB, geometry->AddressOfBlock(checkpointBlock) + geometry->HeaderPartBOffset(), [this]
            {
                WriteCheckpoint();
            });
    }

    void NorFormatter::WriteCheckpoint()
    {
        auto checkpoint = Rounded(geometry->CheckpointBytes());
        auto recordsOffset = geometry->HeaderSize() + static_cast<uint32_t>(checkpoint.size());
        auto rootNode = geometry->AddressOfBlock(nodeBlock) + geometry->NodeSlotsOffset();
        SerializeNorCheckpoint(NorCheckpoint{ rootNode, 1, rootInode + 1, checkpointBlock, recordsOffset, 1, geometry->blocks }, checkpoint);
        for (auto used : std::array<uint32_t, 4>{ 0, 1, nodeBlock, checkpointBlock })
            MarkNorBlockInUse(checkpoint, used);
        SealNorCheckpoint(checkpoint);
        Program(checkpoint, geometry->AddressOfBlock(checkpointBlock) + geometry->HeaderSize(), [this]
            {
                block = 0;
                WriteSuperblock();
            });
    }

    void NorFormatter::WriteSuperblock()
    {
        auto superblock = Rounded(norSuperblockBytes);
        SerializeNorSuperblock(NorSuperblock{ geometry->sectorsPerBlock, geometry->sectorSize, geometry->blocks, geometry->programUnit, geometry->nodeSize, config.inlineBytes, config.maxNameBytes }, superblock);
        Program(superblock, geometry->AddressOfBlock(block), [this]
            {
                if (++block != superblocks)
                    WriteSuperblock();
                else
                    Complete(Result::success);
            });
    }

    void NorFormatter::ReadHeader(uint32_t headerBlock, const infra::Function<void()>& onRead)
    {
        onDeviceDone = onRead;
        flash.ReadBuffer(infra::Head(buffer, geometry->HeaderSize()), geometry->AddressOfBlock(headerBlock), [this]
            {
                onDeviceDone();
            });
    }

    void NorFormatter::EraseSectors(uint32_t first, uint32_t end, const infra::Function<void()>& onErasedDone)
    {
        sector = first;
        sectorEnd = end;
        onErased = onErasedDone;
        EraseNextSector();
    }

    void NorFormatter::EraseNextSector()
    {
        if (sector == sectorEnd)
        {
            onErased();
            return;
        }

        flash.EraseSectors(sector, sector + 1, [this]
            {
                ++sector;
                EraseNextSector();
            });
    }

    void NorFormatter::Program(infra::ConstByteRange bytes, uint32_t address, const infra::Function<void()>& onProgrammed)
    {
        onDeviceDone = onProgrammed;
        flash.WriteBuffer(bytes, address, [this]
            {
                onDeviceDone();
            });
    }

    void NorFormatter::Complete(Result completionResult)
    {
        result = completionResult;
        infra::EventDispatcher::Instance().Schedule([this]
            {
                onDone(result);
            });
    }

    bool NorFormatter::HasEqualSectors() const
    {
        auto sectors = flash.NumberOfSectors();
        if (sectors == 0)
            return false;

        for (uint32_t index = 1; index != sectors; ++index)
            if (flash.SizeOfSector(index) != flash.SizeOfSector(0))
                return false;

        return true;
    }

    uint32_t NorFormatter::AverageEraseCount() const
    {
        if (eraseCountsFound == 0)
            return 0;

        return static_cast<uint32_t>(eraseCountSum / eraseCountsFound);
    }

    infra::ByteRange NorFormatter::Rounded(uint32_t size) const
    {
        auto bytes = infra::Head(buffer, geometry->RoundToProgramUnit(size));
        std::ranges::fill(bytes, uint8_t{ 0xff });
        return bytes;
    }
}
