#ifndef FS_FAT_FAT_ALLOCATOR_HPP
#define FS_FAT_FAT_ALLOCATOR_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatFreeSpaceScan.hpp"
#include "fs/fat/FatReservation.hpp"
#include "fs/fat/FatTableWriter.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    enum class FatChain : uint8_t
    {
        referenced,
        unreferenced
    };

    class FatAllocator
    {
    public:
        FatAllocator(SectorCache& cache, FatVolumeState& volume, FatTableWriter& writer, const FatReservations& reservations, FatFreeSpaceScan& freeSpace);
        FatAllocator(const FatAllocator& other) = delete;
        FatAllocator& operator=(const FatAllocator& other) = delete;
        ~FatAllocator();

        void Reset();
        uint32_t Cursor() const;
        uint32_t Contiguous() const;
        void Grow(std::optional<uint32_t> last, uint32_t clusters, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone);
        void Grow(std::optional<uint32_t> last, uint32_t clusters, FatReservation& reservation, FatChain chain, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone);
        void GrowAt(uint32_t start, uint32_t clusters, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone);
        void Take(uint32_t last, uint32_t clusters, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone);
        void Link(const infra::Function<void(Result result)>& onDone);

    private:
        void Start(std::optional<uint32_t> last, uint32_t start, uint32_t clusters, bool link, FatReservation* reservation, FatChain chain, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone);
        void Next();
        void ReadCandidateSector();
        void OnSector(Result result, infra::ConstByteRange contents);
        std::optional<uint32_t> FreeInSector(infra::ConstByteRange contents);
        void Take(uint32_t cluster);
        void Taken(Result result, uint32_t cluster);
        void Finish(Result result);
        void FinishAllocated(Result result);
        void LinkAfterBarrier();
        void LinkLast();
        void Done(Result result);
        uint32_t Preferred() const;
        bool IsCluster(uint32_t cluster) const;
        bool Available(uint32_t cluster) const;
        uint32_t After(uint32_t cluster) const;

    private:
        SectorCache& cache;
        FatVolumeState& volume;
        FatTableWriter& writer;
        const FatReservations& reservations;
        FatFreeSpaceScan& freeSpace;
        FatReservation* own{ nullptr };
        FatChain chain{ FatChain::referenced };
        uint32_t cursor{ 2 };
        std::optional<uint32_t> last;
        uint32_t start{ 0 };
        uint32_t wanted{ 0 };
        uint32_t allocated{ 0 };
        uint32_t first{ 0 };
        uint32_t previous{ 0 };
        uint32_t contiguous{ 0 };
        uint32_t candidate{ 0 };
        uint32_t examined{ 0 };
        bool preferredOnly{ false };
        bool barrier{ false };
        bool linkWhenTaken{ false };
        Result takeResult{ Result::success };
        infra::AutoResetFunction<void(Result result, uint32_t first, uint32_t allocated)> onDone;
        infra::AutoResetFunction<void(Result result)> onLinked;
    };
}

#endif
