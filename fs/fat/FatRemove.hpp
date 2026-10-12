#ifndef FS_FAT_FAT_REMOVE_HPP
#define FS_FAT_FAT_REMOVE_HPP

#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatEntrySetClear.hpp"
#include "fs/fat/FatParentDirectory.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class FatVolume;

    class FatRemove
        : private FatDirectoryVisitor
    {
    public:
        FatRemove(FatVolume& volume, const FatRemoveCall& call, const infra::Function<void(Result result)>& onFinished);
        FatRemove(const FatRemove& other) = delete;
        FatRemove& operator=(const FatRemove& other) = delete;
        ~FatRemove();

        void Start();

    private:
        bool Visit(const FatScannedEntry& entry) override;
        void FreeSlot(FatEntryPosition position, bool end) override;

        void OnFound(Result result);
        Result Check() const;
        bool InUse() const;
        void CheckEmpty();
        void OnChecked(Result result);
        void ClearEntry();
        void OnCleared(Result result);
        void OnFlushed(Result result);
        void OnFreed(Result result);
        void Finish(Result result);

    private:
        FatVolume& volume;
        FatRemoveCall call;
        infra::Function<void(Result result)> onFinished;
        FatParentDirectory parent;
        FatEntrySetClear clear;
        FatScannedEntry target;
        FatDirectoryCursor cursor;
        bool empty{ true };
    };
}

#endif
