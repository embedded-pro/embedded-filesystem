#ifndef FS_NOR_NOR_CREATE_HPP
#define FS_NOR_NOR_CREATE_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorPathWalk.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorCreate
    {
    public:
        explicit NorCreate(NorVolume& volume);
        NorCreate(const NorCreate& other) = delete;
        NorCreate& operator=(const NorCreate& other) = delete;
        ~NorCreate();

        void Create(infra::BoundedConstString path, NorInodeType type, const infra::Function<Result()>& prepare, const infra::Function<void(Result result)>& onDone);
        uint32_t Inode() const;
        const NorFoundInode& Found() const;
        infra::ConstByteRange FoundInlineBytes() const;

    private:
        enum class Step : uint8_t
        {
            parentFound,
            nameLookedUp,
            inodeFetched,
            parentFetched,
            entryFetched,
            checkpointed,
            committed,
            finish
        };

        void Start();
        void ParentFound(Result result);
        void NameLookedUp(Result result);
        void FetchPaths();
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, Step next);
        void Fetched();
        void CheckRoom();
        void CheckpointAndRestart();
        void Checkpointed(Result result);
        void Commit();
        void Committed(Result result);
        void Finish(Result result);
        const infra::Function<void(Result result)>& Then(Step next);
        void Resume(Result result);
        NorMapId InodeTable() const;
        NorMapId DirectoryMap() const;

    private:
        NorVolume& volume;
        infra::BoundedConstString path;
        NorInodeType type{ NorInodeType::file };
        Step step{ Step::parentFound };
        infra::BoundedConstString name;
        uint32_t parent{ 0 };
        NorMapRoot parentMap;
        uint32_t key{ 0 };
        uint32_t inode{ 0 };
        NorFoundInode found;
        infra::ConstByteRange foundInlineBytes;
        infra::Function<Result()> prepare;
        infra::Function<void(Result result)> resume;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
