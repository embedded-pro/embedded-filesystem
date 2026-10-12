#ifndef FS_FAT_FAT_STAT_HPP
#define FS_FAT_FAT_STAT_HPP

#include "fs/fat/FatPathSlot.hpp"
#include "fs/fat/FatVolume.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class FatStat
    {
    public:
        FatStat(FatVolume& volume, const FatStatCall& call, const infra::Function<void(Result result)>& onFinished);
        FatStat(const FatStat& other) = delete;
        FatStat& operator=(const FatStat& other) = delete;
        ~FatStat();

        void Start();

    private:
        void OnFound(Result result);

    private:
        FatVolume& volume;
        FatStatCall call;
        infra::Function<void(Result result)> onFinished;
    };
}

#endif
