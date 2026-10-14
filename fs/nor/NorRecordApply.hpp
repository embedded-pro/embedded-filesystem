#ifndef FS_NOR_NOR_RECORD_APPLY_HPP
#define FS_NOR_NOR_RECORD_APPLY_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/interfaces/Result.hpp"
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
        void ApplyNext();
        void PutInode(const NorInodePut& put);
        void InodeFetched();
        void PutDirectoryEntry(const NorDirectoryEntryPut& put);
        void DirectoryFetched();
        void EntryFetched();
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, const infra::Function<void()>& onFetchedDone);
        NorMapId InodeTable() const;
        NorMapId DirectoryMap() const;

    private:
        NorVolume& volume;
        std::optional<NorEntryReader> reader;
        bool linked{ false };
        bool first{ false };
        NorInodePut inodePut;
        NorDirectoryEntryPut entryPut;
        NorMapRoot directoryMap;
        infra::AutoResetFunction<void()> onFetched;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
