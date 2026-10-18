#ifndef FS_FAT_FAT_WRITE_HPP
#define FS_FAT_FAT_WRITE_HPP

#include "fs/fat/FatExtentCache.hpp"
#include "fs/fat/FatOpenFile.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

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
        enum class Step : uint8_t
        {
            dirtied,
            located,
            lastLocated,
            transferred
        };

        void Next();
        void Grow();
        void Allocate(std::optional<uint32_t> start, std::optional<uint32_t> last);
        void OnGrown(Result result, uint32_t first, uint32_t allocated);
        void Locate(uint32_t first, uint32_t last, Step next);
        void Located(Result result, const FatExtent& run);
        void Transfer(const FatExtent& run);
        void TransferSectors(uint32_t sector, uint32_t sectors);
        void TransferSector(uint32_t sector, uint32_t offset, uint32_t bytes);
        void Transferred(Result result);
        const infra::Function<void(Result result)>& Then(Step next);
        void Resume(Result result);
        uint32_t SectorSize() const;
        uint32_t ClusterBytes() const;
        uint32_t Needed() const;
        std::optional<uint32_t> LogStart() const;

    private:
        FatVolume& volume;
        FatOpenFile& file;
        FatExtentCache& extents;
        infra::ConstByteRange data;
        Result endResult{ Result::success };
        Step step{ Step::dirtied };
        uint32_t partOffset{ 0 };
        uint32_t partBytes{ 0 };
        infra::Function<void(Result result)> onDone;
        infra::Function<void(Result result)> resume;
    };
}

#endif
