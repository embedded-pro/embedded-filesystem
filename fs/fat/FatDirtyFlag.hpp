#ifndef FS_FAT_FAT_DIRTY_FLAG_HPP
#define FS_FAT_FAT_DIRTY_FLAG_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatDirtyFlag
    {
    public:
        FatDirtyFlag(SectorCache& cache, const FatVolumeState& volume);
        FatDirtyFlag(const FatDirtyFlag& other) = delete;
        FatDirtyFlag& operator=(const FatDirtyFlag& other) = delete;
        ~FatDirtyFlag();

        void Reset();
        void MarkDirty(const infra::Function<void(Result result)>& onDone);
        void MarkClean(const infra::Function<void(Result result)>& onDone);
        void Skip(const infra::Function<void(Result result)>& onDone);

    private:
        enum class Mark : uint8_t
        {
            untouched,
            leftDirty,
            markedDirty
        };

        void OnFirstSector(Result result, infra::ConstByteRange contents);
        void ModifyFirstSector();
        void OnFirstSectorModified(Result result, infra::ByteRange contents);
        void OnFlushed(Result result);
        void WriteCleanBit(infra::ByteRange contents, bool clean) const;
        void CompleteLater();
        uint32_t FirstSector() const;

    private:
        SectorCache& cache;
        const FatVolumeState& volume;
        Mark mark{ Mark::untouched };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
