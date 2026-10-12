#ifndef FS_FAT_FAT_CHAIN_FREE_HPP
#define FS_FAT_FAT_CHAIN_FREE_HPP

#include "fs/common/DirectTransfers.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatReservation.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/fat/FatTableWriter.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatChainFree
    {
    public:
        FatChainFree(SectorCache& cache, DirectTransfers& transfers, FatVolumeState& volume, FatTableWriter& writer, bool discard);
        FatChainFree(const FatChainFree& other) = delete;
        FatChainFree& operator=(const FatChainFree& other) = delete;
        ~FatChainFree();

        void Free(uint32_t first, const infra::Function<void(Result result)>& onDone);

    private:
        void ReadNext();
        void OnNext(Result result, std::optional<uint32_t> next);
        void OnFreed(Result result);
        void DiscardRun();
        void OnDiscarded(Result result);

    private:
        DirectTransfers& transfers;
        FatVolumeState& volume;
        FatTableWriter& writer;
        FatTableReader table;
        bool discard;
        uint32_t current{ 0 };
        std::optional<uint32_t> next;
        FatReservation run;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
