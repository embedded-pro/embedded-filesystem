#ifndef FS_NOR_NOR_REMOVE_HPP
#define FS_NOR_NOR_REMOVE_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorRemove
    {
    public:
        NorRemove(NorVolume& volume, NorRemoveCall&& call, const infra::Function<void(Result result)>& onFinished);
        NorRemove(const NorRemove& other) = delete;
        NorRemove& operator=(const NorRemove& other) = delete;
        ~NorRemove();

        void Start();

    private:
        void ParentFound(Result result);
        void NameLookedUp(Result result);
        bool IsOpen() const;
        void FetchPaths();
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t fetchKey, const infra::Function<void()>& onFetchedDone);
        void CheckRoom();
        void CheckpointAndRestart();
        void Commit();
        void Committed(Result result);
        void Finish(Result result);
        NorMapId InodeTable() const;
        NorMapId DirectoryMap() const;

    private:
        NorVolume& volume;
        NorRemoveCall call;
        infra::Function<void(Result result)> onFinished;
        infra::BoundedConstString name;
        uint32_t parent{ 0 };
        NorMapRoot parentMap;
        uint32_t key{ 0 };
        uint32_t inode{ 0 };
        NorMapRoot map;
        infra::AutoResetFunction<void()> onFetched;
    };
}

#endif
