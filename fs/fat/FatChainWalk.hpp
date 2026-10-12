#ifndef FS_FAT_FAT_CHAIN_WALK_HPP
#define FS_FAT_FAT_CHAIN_WALK_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatExtentCache.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatChainWalk
    {
    public:
        FatChainWalk(SectorCache& cache, const FatGeometry& geometry);
        FatChainWalk(const FatChainWalk& other) = delete;
        FatChainWalk& operator=(const FatChainWalk& other) = delete;
        ~FatChainWalk();

        void Walk(const FatExtent& from, uint32_t target, uint32_t limit, const infra::Function<void(Result result, const FatExtent& run)>& onDone);

    private:
        void Step();
        void OnNext(Result result, std::optional<uint32_t> next);
        bool Covered() const;

    private:
        FatTableReader table;
        const FatGeometry& geometry;
        FatExtent run;
        uint32_t target{ 0 };
        uint32_t limit{ 0 };
        uint32_t steps{ 0 };
        infra::AutoResetFunction<void(Result result, const FatExtent& run)> onDone;
    };
}

#endif
