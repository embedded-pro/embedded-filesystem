#ifndef FS_FAT_FAT_RENAME_HPP
#define FS_FAT_FAT_RENAME_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatEntrySetClear.hpp"
#include "fs/fat/FatParentDirectory.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatVolume;

    class FatRename
    {
    public:
        FatRename(FatVolume& volume, FatRenameCall&& call, const infra::Function<void(Result result)>& onFinished);
        FatRename(const FatRename& other) = delete;
        FatRename& operator=(const FatRename& other) = delete;
        ~FatRename();

        void Start();

    private:
        using Step = void (FatRename::*)();

        void OnSourceFound(Result result);
        void OnTargetFound(Result result);
        bool SameName() const;
        void OnPrepared(Result result);
        void ClearSource();
        void UpdateParentEntry();
        bool Moved() const;
        bool SetParent(infra::ByteRange contents) const;
        void Succeed();
        void FlushThen(Result result, Step next);
        void Finish(Result result);

    private:
        FatVolume& volume;
        FatRenameCall call;
        infra::Function<void(Result result)> onFinished;
        FatParentDirectory parent;
        FatEntrySetClear clear;
        FatScannedEntry source;
        uint32_t sourceParent{ 0 };
        Step afterFlush{ nullptr };
    };
}

#endif
