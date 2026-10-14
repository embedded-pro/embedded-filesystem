#ifndef FS_NOR_NOR_MAKE_DIRECTORY_HPP
#define FS_NOR_NOR_MAKE_DIRECTORY_HPP

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

    class NorMakeDirectory
    {
    public:
        NorMakeDirectory(NorVolume& volume, NorMakeDirectoryCall&& call, const infra::Function<void(Result result)>& onFinished);
        NorMakeDirectory(const NorMakeDirectory& other) = delete;
        NorMakeDirectory& operator=(const NorMakeDirectory& other) = delete;
        ~NorMakeDirectory();

        void Start();

    private:
        void ParentFound(Result result);
        void NameLookedUp(Result result);
        void FetchPaths();
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, const infra::Function<void()>& onFetchedDone);
        void CheckRoom();
        void CheckpointAndRestart();
        void Commit();
        void Committed(Result result);
        void Finish(Result result);
        NorMapId InodeTable() const;
        NorMapId DirectoryMap() const;

    private:
        NorVolume& volume;
        NorMakeDirectoryCall call;
        infra::Function<void(Result result)> onFinished;
        infra::BoundedConstString name;
        uint32_t parent{ 0 };
        NorMapRoot parentMap;
        uint32_t key{ 0 };
        uint32_t inode{ 0 };
        infra::AutoResetFunction<void()> onFetched;
    };
}

#endif
