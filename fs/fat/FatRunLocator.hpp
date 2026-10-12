#ifndef FS_FAT_FAT_RUN_LOCATOR_HPP
#define FS_FAT_FAT_RUN_LOCATOR_HPP

#include "fs/fat/FatChainWalk.hpp"
#include "fs/fat/FatExtentCache.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatRunLocator
    {
    public:
        explicit FatRunLocator(FatChainWalk& chain);
        FatRunLocator(const FatRunLocator& other) = delete;
        FatRunLocator& operator=(const FatRunLocator& other) = delete;
        ~FatRunLocator();

        void Locate(FatExtentCache& extents, uint32_t index, uint32_t limit, const infra::Function<void(Result result, const FatExtent& run)>& onDone);

    private:
        FatChainWalk& chain;
        FatExtentCache* extents{ nullptr };
        infra::AutoResetFunction<void(Result result, const FatExtent& run)> onDone;
    };
}

#endif
