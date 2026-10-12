#ifndef FS_FAT_FAT_SYNC_HPP
#define FS_FAT_FAT_SYNC_HPP

#include "fs/fat/FatOpenFile.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class FatVolume;

    class FatSync
    {
    public:
        FatSync(FatVolume& volume, FatOpenFile& file, const infra::Function<void(Result result)>& onDone);
        FatSync(const FatSync& other) = delete;
        FatSync& operator=(const FatSync& other) = delete;
        ~FatSync();

        void Start();

    private:
        void Flush(Result result);

    private:
        FatVolume& volume;
        FatOpenFile& file;
        infra::Function<void(Result result)> onDone;
    };
}

#endif
