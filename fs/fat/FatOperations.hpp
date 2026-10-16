#ifndef FS_FAT_FAT_OPERATIONS_HPP
#define FS_FAT_FAT_OPERATIONS_HPP

#include "fs/fat/FatPathSlot.hpp"
#include "fs/fat/FatTimestamp.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatDirtyFlag;
    class FatExtentCache;
    struct FatLongNames;
    class FatMount;
    class FatVolume;
    struct FatOpenFile;

    template<class Call>
    using FatPathOperation = void (*)(FatVolume& volume, Call&& call, const infra::Function<void(Result result)>& onFinished);
    using FatExpectSizeOperation = void (*)(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, uint32_t size, const infra::Function<void(Result result)>& onDone);
    using FatClock = FatDateTime (*)(uint32_t timerServiceId);
    using FatDirtyMark = void (*)(FatDirtyFlag& flag, const infra::Function<void(Result result)>& onDone);
    using FatCleanState = void (*)(FatMount& mount);

    struct FatOperations
    {
        FatPathOperation<FatMakeDirectoryCall> makeDirectory{ nullptr };
        FatPathOperation<FatRemoveCall> remove{ nullptr };
        FatPathOperation<FatRenameCall> rename{ nullptr };
        FatExpectSizeOperation expectSize{ nullptr };
        FatPathOperation<FatUsageCall> usage{ nullptr };
        FatClock now{ nullptr };
        FatDirtyMark markDirty{ nullptr };
        FatDirtyMark markClean{ nullptr };
        FatCleanState readCleanState{ nullptr };
        const FatLongNames* longNames{ nullptr };
    };
}

#endif
