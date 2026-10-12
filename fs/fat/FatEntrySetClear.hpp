#ifndef FS_FAT_FAT_ENTRY_SET_CLEAR_HPP
#define FS_FAT_FAT_ENTRY_SET_CLEAR_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace fs
{
    class FatEntrySetClear
    {
    public:
        FatEntrySetClear(SectorCache& cache, FatDirectoryReader& reader, const FatGeometry& geometry);
        FatEntrySetClear(const FatEntrySetClear& other) = delete;
        FatEntrySetClear& operator=(const FatEntrySetClear& other) = delete;
        ~FatEntrySetClear();

        void Clear(const FatScannedEntry& entry, const infra::Function<void(Result result)>& onDone);

    private:
        void OnMiddle(Result result, std::optional<uint32_t> next);
        void ClearSector(uint32_t sector, uint8_t rank);
        void OnCleared(Result result);
        void ClearNextSector();
        void Mark(infra::ByteRange contents) const;
        FatEntryPosition PositionOf(uint32_t slot) const;
        uint32_t SectorOfSlot(uint32_t slot) const;

    private:
        SectorCache& cache;
        FatDirectoryReader& reader;
        FatTableReader table;
        FatEntryPosition first{};
        FatEntryPosition shortEntry{};
        uint32_t slots{ 0 };
        uint32_t capacity{ 0 };
        std::array<uint32_t, 3> clusters{};
        uint32_t sector{ 0 };
        uint32_t slot{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
