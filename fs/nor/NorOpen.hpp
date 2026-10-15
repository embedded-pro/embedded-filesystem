#ifndef FS_NOR_NOR_OPEN_HPP
#define FS_NOR_NOR_OPEN_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorCreate.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "fs/nor/NorPathWalk.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstdint>

namespace fs
{
    class NorFile;
    class NorVolume;

    class NorOpen
    {
    public:
        using Allocate = infra::Function<infra::SharedPtr<NorFile>()>;

        NorOpen(NorVolume& volume, NorOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished);
        NorOpen(const NorOpen& other) = delete;
        NorOpen& operator=(const NorOpen& other) = delete;
        ~NorOpen();

        void Start();

    private:
        void OnFound(Result result);
        void OnCreated(Result result);
        void OpenExisting(const NorFoundInode& found);
        void TailFetched(NorFetchResult result);
        bool IsDataBlock(uint32_t block) const;
        Result Allocated();
        Result Shared(uint32_t inode) const;
        void Finish(Result result);

    private:
        NorVolume& volume;
        NorOpenCall call;
        Allocate allocate;
        infra::Function<void(Result result)> onFinished;
        NorCreate create;
        infra::SharedPtr<NorFile> file;
        NorOpenFile opened;
        NorMapRoot map;
        uint32_t lastIndex{ 0 };
    };
}

#endif
