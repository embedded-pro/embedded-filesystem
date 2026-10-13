#ifndef FS_FAT_FAT_TRUNCATION_HPP
#define FS_FAT_FAT_TRUNCATION_HPP

#include "fs/fat/FatExtentCache.hpp"
#include "fs/fat/FatOpenFile.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatVolume;

    class FatTruncation
    {
    public:
        explicit FatTruncation(FatVolume& volume);
        FatTruncation(const FatTruncation& other) = delete;
        FatTruncation& operator=(const FatTruncation& other) = delete;
        ~FatTruncation();

        void Truncate(FatOpenFile& file, FatExtentCache& extents, const infra::Function<void(Result result)>& onDone);

    private:
        using Step = void (FatTruncation::*)();

        void LocateLastKept();
        void ReadFirstCut(uint32_t cluster);
        void Commit();
        void CutChain();
        void FreeCut();
        void Succeed();
        void FlushThen(Result result, Step next);

    private:
        FatVolume& volume;
        FatTableReader table;
        FatOpenFile* file{ nullptr };
        FatExtentCache* extents{ nullptr };
        uint32_t kept{ 0 };
        uint32_t lastKept{ 0 };
        std::optional<uint32_t> firstCut;
        Step afterFlush{ nullptr };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
