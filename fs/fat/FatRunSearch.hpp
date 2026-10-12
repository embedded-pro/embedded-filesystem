#ifndef FS_FAT_FAT_RUN_SEARCH_HPP
#define FS_FAT_FAT_RUN_SEARCH_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatAllocationUnits.hpp"
#include "fs/fat/FatReservation.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatRunSearch
    {
    public:
        FatRunSearch(SectorCache& cache, const FatVolumeState& volume, const FatReservations& reservations);
        FatRunSearch(const FatRunSearch& other) = delete;
        FatRunSearch& operator=(const FatRunSearch& other) = delete;
        ~FatRunSearch();

        void Find(std::optional<uint32_t> after, uint32_t cursor, uint32_t wanted, const std::optional<FatAllocationUnits>& units, const infra::Function<void(Result result, FatReservation run)>& onDone);

    private:
        void ReadSector();
        void OnSector(Result result, infra::ConstByteRange contents);
        bool Adjacent(uint32_t cluster, bool free);
        bool Search(uint32_t cluster, bool free);
        bool Reached();
        bool Finish(FatReservation run);
        bool IsFree(uint32_t cluster, infra::ConstByteRange contents) const;
        uint32_t SectorOf(uint32_t cluster) const;
        uint32_t After(uint32_t cluster) const;

    private:
        SectorCache& cache;
        const FatVolumeState& volume;
        const FatReservations& reservations;
        std::optional<FatAllocationUnits> units;
        uint32_t cursor{ 0 };
        uint32_t wanted{ 0 };
        uint32_t target{ 0 };
        uint32_t candidate{ 0 };
        uint32_t examined{ 0 };
        bool adjacent{ false };
        FatReservation run;
        FatReservation best;
        infra::AutoResetFunction<void(Result result, FatReservation run)> onDone;
    };
}

#endif
