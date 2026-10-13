#ifndef FS_FAT_FAT_FREE_SPACE_SCAN_HPP
#define FS_FAT_FAT_FREE_SPACE_SCAN_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatFreeSpaceScan
    {
    public:
        FatFreeSpaceScan(SectorCache& cache, FatVolumeState& volume);
        FatFreeSpaceScan(const FatFreeSpaceScan& other) = delete;
        FatFreeSpaceScan& operator=(const FatFreeSpaceScan& other) = delete;
        ~FatFreeSpaceScan();

        void Restart();
        bool Done() const;
        void Step(const infra::Function<void(Result result)>& onDone);
        void Allocated(uint32_t taken);
        void Freed(uint32_t released);

    private:
        void OnSector(Result result, infra::ConstByteRange contents);
        void Count(infra::ConstByteRange contents);

    private:
        SectorCache& cache;
        FatVolumeState& volume;
        uint32_t sector{ 0 };
        uint32_t cluster{ 2 };
        uint32_t freeClusters{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
