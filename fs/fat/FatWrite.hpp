#ifndef FS_FAT_FAT_WRITE_HPP
#define FS_FAT_FAT_WRITE_HPP

#include "fs/fat/FatExtentCache.hpp"
#include "fs/fat/FatOpenFile.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatVolume;

    class FatWrite
    {
    public:
        FatWrite(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone);
        FatWrite(const FatWrite& other) = delete;
        FatWrite& operator=(const FatWrite& other) = delete;
        ~FatWrite();

        void Start();

    private:
        void Step();
        void Grow();
        void GrowAfter(std::optional<uint32_t> last);
        void OnGrown(Result result, uint32_t first, uint32_t allocated);
        void Transfer(const FatExtent& run);
        void TransferSectors(uint32_t sector, uint32_t sectors);
        void TransferSector(uint32_t sector, uint32_t offset, uint32_t bytes);
        void Transferred(Result result, uint32_t bytes);
        uint32_t SectorSize() const;
        uint32_t ClusterBytes() const;

    private:
        FatVolume& volume;
        FatOpenFile& file;
        FatExtentCache& extents;
        infra::ConstByteRange data;
        Result endResult{ Result::success };
        uint32_t partOffset{ 0 };
        uint32_t partBytes{ 0 };
        infra::Function<void(Result result)> onDone;
    };
}

#endif
