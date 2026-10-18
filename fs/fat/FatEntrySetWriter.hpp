#ifndef FS_FAT_FAT_ENTRY_SET_WRITER_HPP
#define FS_FAT_FAT_ENTRY_SET_WRITER_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatAllocator.hpp"
#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatNameScan.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatLongNames;

    struct FatEntrySet
    {
        infra::BoundedConstString longName;
        ShortEntry entry;
    };

    class FatEntrySetWriter
    {
    public:
        FatEntrySetWriter(SectorCache& cache, FatDirectoryReader& reader, FatAllocator& allocator, const FatGeometry& geometry, const FatLongNames* longNames);
        FatEntrySetWriter(const FatEntrySetWriter& other) = delete;
        FatEntrySetWriter& operator=(const FatEntrySetWriter& other) = delete;
        ~FatEntrySetWriter();

        void Write(const FatEntrySet& set, const FatFreeSlots& slots, const infra::Function<void(Result result)>& onDone);
        FatScannedEntry Written() const;

    private:
        void Resolve();
        void OnNext(Result result, std::optional<uint32_t> next);
        void Grow();
        void OnTaken(Result result, uint32_t cluster);
        void ZeroSector();
        void OnZeroed(Result result);
        void OnLinked(Result result);
        void WriteSector();
        void UpdateSector();
        void OnWritten(Result result);
        void Fill(infra::ByteRange contents) const;
        void Done(Result result);
        uint32_t ClusterIndexOf(uint32_t slot) const;
        FatEntryPosition PositionOf(uint32_t slot) const;
        uint32_t SectorOfSlot(uint32_t slot) const;
        uint8_t RankOf(uint32_t sector) const;

    private:
        SectorCache& cache;
        FatDirectoryReader& reader;
        FatAllocator& allocator;
        const FatGeometry& geometry;
        const FatLongNames* longNames;
        FatTableReader table;
        FatEntrySet set;
        FatFreeSlots start{};
        uint32_t capacity{ 0 };
        uint8_t slots{ 1 };
        uint8_t checksum{ 0 };
        bool zeroing{ false };
        std::array<uint32_t, 3> clusters{};
        uint8_t resolved{ 0 };
        uint8_t existing{ 0 };
        uint32_t sector{ 0 };
        uint32_t slot{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
