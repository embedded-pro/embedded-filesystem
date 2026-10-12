#ifndef FS_FAT_FAT_READ_HPP
#define FS_FAT_FAT_READ_HPP

#include "fs/fat/FatExtentCache.hpp"
#include "fs/fat/FatOpenFile.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class FatVolume;

    class FatRead
    {
    public:
        FatRead(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone);
        FatRead(const FatRead& other) = delete;
        FatRead& operator=(const FatRead& other) = delete;
        ~FatRead();

        void Start();

    private:
        void Locate();
        void Transfer(const FatExtent& run);
        void TransferSectors(uint32_t sector, uint32_t sectors);
        void TransferPart(uint32_t sector, uint32_t offset, uint32_t bytes);
        void Transferred(Result result, uint32_t bytes);
        uint32_t Remaining() const;
        uint32_t SectorSize() const;
        uint32_t ClusterBytes() const;

    private:
        FatVolume& volume;
        FatOpenFile& file;
        FatExtentCache& extents;
        infra::ByteRange buffer;
        std::size_t bytesRead{ 0 };
        uint32_t partOffset{ 0 };
        uint32_t partBytes{ 0 };
        infra::Function<void(Result result, std::size_t bytesRead)> onDone;
    };
}

#endif
