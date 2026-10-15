#ifndef FS_NOR_NOR_RENAME_HPP
#define FS_NOR_NOR_RENAME_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorRename
    {
    public:
        NorRename(NorVolume& volume, NorRenameCall&& call, const infra::Function<void(Result result)>& onFinished);
        NorRename(const NorRename& other) = delete;
        NorRename& operator=(const NorRename& other) = delete;
        ~NorRename();

        void Start();

    private:
        bool IsIntoOwnSubtree() const;
        void FromParentFound(Result result);
        void FromLookedUp(Result result);
        bool IsOpen() const;
        void ToParentFound(Result result);
        void ToLookedUp(Result result);
        void FetchPaths();
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t fetchKey, const infra::Function<void()>& onFetchedDone);
        void CheckRoom();
        void CheckpointAndRestart();
        void Commit();
        void AddEntries(CrcRecord& record) const;
        void Committed(Result result);
        void Finish(Result result);
        std::size_t RecordBytes() const;
        NorMapId InodeTable() const;
        NorMapId DirectoryMap(uint32_t directory) const;

    private:
        NorVolume& volume;
        NorRenameCall call;
        infra::Function<void(Result result)> onFinished;
        infra::BoundedConstString fromName;
        infra::BoundedConstString toName;
        uint32_t fromParent{ 0 };
        NorMapRoot fromParentMap;
        uint32_t fromKey{ 0 };
        uint32_t inode{ 0 };
        uint32_t toParent{ 0 };
        NorMapRoot toParentMap;
        uint32_t toKey{ 0 };
        infra::AutoResetFunction<void()> onFetched;
    };
}

#endif
