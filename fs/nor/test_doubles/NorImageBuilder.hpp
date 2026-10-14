#ifndef FS_NOR_TEST_DOUBLES_NOR_IMAGE_BUILDER_HPP
#define FS_NOR_TEST_DOUBLES_NOR_IMAGE_BUILDER_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorGeometry.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/test_doubles/NorImage.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/ByteRange.hpp"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace fs
{
    class NorImageBuilder
    {
    public:
        NorImageBuilder(NorImage& image, const NorConfig& config);

        const NorGeometry& Geometry() const;
        uint32_t CheckpointBlock() const;
        uint32_t JournalBlock() const;
        uint32_t JournalEnd() const;
        uint64_t NextSequence() const;
        NorCheckpoint Checkpoint() const;

        void SetEraseCount(uint32_t block, uint32_t count);
        void EraseBlock(uint32_t block);
        uint64_t Allocate(uint32_t block, NorBlockType type, NorBlockOwner owner = {});
        void WriteCheckpoint(uint32_t block);
        void WriteCheckpoint(uint32_t block, const NorCheckpoint& checkpoint, std::initializer_list<uint32_t> used);
        void AppendRecord(std::initializer_list<NorEntry> entries);
        uint32_t AppendRecord(uint32_t block, uint32_t offset, std::initializer_list<NorEntry> entries);
        void StartJournalBlock(uint32_t block);
        void Program(uint32_t address, infra::ConstByteRange bytes);
        void Read(uint32_t address, infra::ByteRange bytes) const;
        void Corrupt(uint32_t address);

    private:
        void FindCheckpoint();
        infra::ByteRange Scratch(uint32_t size);
        uint32_t ProgramRecord(uint32_t block, uint32_t offset, const CrcRecord& record);
        void ProgramCheckpoint(uint32_t block, infra::ByteRange bytes);
        void SetInUse(infra::ByteRange checkpoint, uint32_t block, bool inUse) const;

    private:
        NorImage& image;
        NorConfig config;
        NorGeometry geometry;
        uint32_t checkpointBlock{ 0 };
        uint32_t journalBlock{ 0 };
        uint32_t journalEnd{ 0 };
        uint64_t nextSequence{ 0 };
        bool linkPending{ false };
        NorLink link;
        infra::BoundedVector<uint32_t>::WithMaxSize<16> epochJournalBlocks;
        mutable std::array<uint8_t, 2048> buffer{};
    };
}

#endif
