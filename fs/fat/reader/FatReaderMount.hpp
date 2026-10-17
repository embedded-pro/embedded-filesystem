#ifndef FS_FAT_READER_FAT_READER_MOUNT_HPP
#define FS_FAT_READER_FAT_READER_MOUNT_HPP

#include "fs/common/SectorSource.hpp"
#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatReaderMount
    {
    public:
        FatReaderMount(hal::BlockDevice& device, SectorSource& source, FatGeometry& geometry);
        FatReaderMount(const FatReaderMount& other) = delete;
        FatReaderMount& operator=(const FatReaderMount& other) = delete;
        ~FatReaderMount();

        void Start(const infra::Function<void(Result result)>& onDone);

    private:
        void OnFirstSector(Result result, infra::ConstByteRange contents);
        void ReadBootSector(uint32_t sector, uint32_t sectors);
        void OnBootSector(Result result, infra::ConstByteRange contents);
        void UseGeometry(const BiosParameterBlock& parameters, uint32_t start, uint32_t sectors);

    private:
        hal::BlockDevice& device;
        SectorSource& source;
        FatGeometry& geometry;
        uint32_t volumeStart{ 0 };
        uint32_t volumeSectors{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
