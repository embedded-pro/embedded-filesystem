#ifndef FS_NOR_NOR_RECORD_APPLY_HPP
#define FS_NOR_NOR_RECORD_APPLY_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorRecord.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorRecordApply
    {
    public:
        explicit NorRecordApply(NorVolume& volume);
        NorRecordApply(const NorRecordApply& other) = delete;
        NorRecordApply& operator=(const NorRecordApply& other) = delete;
        ~NorRecordApply();

        void Apply(const OpenedRecord& record, bool linked, const infra::Function<void(Result result)>& onDone);

    private:
        enum class Step : uint8_t
        {
            inodeFetched,
            removedInodeFetched,
            removeRecord,
            recordFetched,
            directoryFetched,
            entryFetched,
            removedEntryFetched,
            fileFetched,
            slotFetched,
            sizedFileFetched,
            sizeReleased,
            writeSize,
            released
        };

        void ApplyNext();
        void PutInode(const NorInodePut& put);
        void InodeFetched();
        void RemoveInode(const NorInodeRemove& remove);
        void RemovedInodeFetched();
        void RemoveRecord();
        void RecordFetched();
        void PutDirectoryEntry(const NorDirectoryEntryPut& put);
        void EntryFetched();
        void RemoveDirectoryEntry(const NorDirectoryEntryRemove& remove);
        void RemovedEntryFetched();
        void FetchDirectory(uint32_t directory, uint32_t key, Step next);
        void DirectoryFetched();
        void SetBlock(const NorBlockSet& set);
        void FileFetched();
        void SlotFetched();
        void SetSize(const NorSize& size);
        void SizedFileFetched();
        void SizeReleased();
        void WriteSize();
        void ReleaseBlocks(uint32_t from, uint32_t to, Step next);
        void ReleaseNextBlock();
        void BlockFetched();
        void Release(uint32_t block) const;
        bool IsOfEpoch(uint32_t block) const;
        uint32_t BlocksOfSize(uint32_t size) const;
        std::optional<infra::ConstByteRange> RecordOfType(uint32_t inode, NorInodeType type) const;
        bool HasRoomFor(uint32_t createdNodes) const;
        bool IsInVolume(uint32_t block) const;
        bool IsFreeBlock(uint32_t block) const;
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, Step next);
        void Continue(Step next);
        NorMapId InodeTable() const;
        NorMapId DirectoryMap(uint32_t directory) const;
        NorMapId DataMap(uint32_t file) const;

    private:
        NorVolume& volume;
        std::optional<NorEntryReader> reader;
        bool linked{ false };
        bool first{ false };
        NorInodePut inodePut;
        uint32_t removedInode{ 0 };
        NorDirectoryEntryPut entryPut;
        NorDirectoryEntryRemove entryRemove;
        uint32_t directoryInode{ 0 };
        uint32_t directoryKey{ 0 };
        NorMapRoot directoryMap;
        NorBlockSet blockSet;
        NorSize sizeEntry;
        uint32_t dataInode{ 0 };
        NorMapRoot dataMap;
        uint32_t releaseFrom{ 0 };
        uint32_t releaseIndex{ 0 };
        bool releasing{ false };
        bool fetchedWhileReleasing{ false };
        Step step{ Step::inodeFetched };
        Step afterDirectory{ Step::entryFetched };
        Step afterRelease{ Step::removeRecord };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
