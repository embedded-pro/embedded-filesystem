#ifndef FS_FAT_FAT_MOUNT_HPP
#define FS_FAT_FAT_MOUNT_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatOperations.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatMount
    {
    public:
        FatMount(hal::BlockDevice& device, SectorCache& cache, FatVolumeState& volume, FatCleanState readCleanState, const infra::Function<void(Result result)>& onDone);
        FatMount(const FatMount& other) = delete;
        FatMount& operator=(const FatMount& other) = delete;
        ~FatMount();

        void Start();
        void ReadCleanState();

    private:
        void ReadFirstSector();
        void OnFirstSector(Result result, infra::ConstByteRange contents);
        void ReadBootSector(uint32_t sector, uint32_t sectors);
        void OnBootSector(Result result, infra::ConstByteRange contents);
        void UseGeometry(const BiosParameterBlock& parameters, uint32_t start, uint32_t sectors);
        void ReadFsInfo();
        void OnFsInfo(Result result, infra::ConstByteRange contents);
        void ReadFatHeader();
        void OnFatHeader(Result result, infra::ConstByteRange contents);
        void Done(Result result);

    private:
        hal::BlockDevice& device;
        SectorCache& cache;
        FatVolumeState& volume;
        FatCleanState readCleanState;
        uint32_t volumeStart{ 0 };
        uint32_t volumeSectors{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
