#ifndef FS_NOR_TEST_DOUBLES_NOR_IMAGE_BUILDER_HPP
#define FS_NOR_TEST_DOUBLES_NOR_IMAGE_BUILDER_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorGeometry.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/test_doubles/NorImage.hpp"
#include "fs/nor/test_doubles/NorImageFlash.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace fs
{
    class NorImageBuilder
    {
    public:
        static constexpr uint32_t rootInode = 1;

        NorImageBuilder(NorImage& image, const NorConfig& config);
        NorImageBuilder(const NorImageBuilder& other) = delete;
        NorImageBuilder& operator=(const NorImageBuilder& other) = delete;
        ~NorImageBuilder() = default;

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

        uint32_t AddDirectory(uint32_t parent, infra::BoundedConstString name);
        uint32_t AddFile(uint32_t parent, infra::BoundedConstString name, infra::ConstByteRange contents);
        void AddRecord(uint32_t inode, const NorInodeRecord& record);
        void AddEntry(uint32_t directory, uint32_t key, uint32_t inode);
        void Commit();

        const NorMapRoot& InodeTable() const;
        NorMapRoot MapOf(uint32_t inode);
        std::optional<NorInodeRecord> RecordOf(uint32_t inode);
        std::optional<uint32_t> EntryOf(uint32_t directory, uint32_t key);

    private:
        uint32_t AddInode(uint32_t parent, infra::BoundedConstString name, const NorInodeRecord& record);
        uint32_t FreeKey(uint32_t directory, infra::BoundedConstString name);
        void SetMapOf(uint32_t inode, const NorMapRoot& map);
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key);
        NorMapId InodeTableId() const;
        uint32_t FreeBlock();
        uint32_t NodeAddress(infra::BoundedVector<uint32_t>& nodeBlocks, uint32_t& offset);
        void MoveCheckpoint(uint32_t block, NorCheckpoint checkpoint, infra::MemoryRange<const uint32_t> blocksInUse);
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
        NorImageFlash flash{ image };
        std::array<NorNodeCache::Slot, 64> slots{};
        std::array<uint8_t, 64 * 512> nodes{};
        NorNodeCache cache;
        NorMapFetch fetch{ cache };
        NorMapRoot inodeTable;
        uint32_t nextInode{ 0 };
        uint32_t liveNodes{ 0 };
    };
}

#endif
