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
        void Read();
        void OnSector(Result result, infra::ConstByteRange contents);
        void UseGeometry(const BiosParameterBlock& parameters);
        void ReadCleanSector();
        void OnCleanSector(Result result, infra::ConstByteRange contents);
        void OnFatHeader(Result result, infra::ConstByteRange contents);
        void Done(Result result);

    private:
        hal::BlockDevice& device;
        SectorCache& cache;
        FatVolumeState& volume;
        FatCleanState readCleanState;
        bool partitioned{ false };
        bool fsInfoRead{ false };
        uint32_t volumeStart{ 0 };
        uint32_t volumeSectors{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
