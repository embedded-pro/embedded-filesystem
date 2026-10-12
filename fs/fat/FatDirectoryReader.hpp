#ifndef FS_FAT_FAT_DIRECTORY_READER_HPP
#define FS_FAT_FAT_DIRECTORY_READER_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatDirectoryVisitor
    {
    protected:
        FatDirectoryVisitor();
        FatDirectoryVisitor(const FatDirectoryVisitor& other) = delete;
        FatDirectoryVisitor& operator=(const FatDirectoryVisitor& other) = delete;
        ~FatDirectoryVisitor() = default;

    public:
        virtual bool Visit(const FatScannedEntry& entry) = 0;
        virtual void FreeSlot(FatEntryPosition position, bool end) = 0;
    };

    struct FatDirectoryCursor
    {
        uint32_t cluster{ 0 };
        uint32_t index{ 0 };
        uint32_t entriesVisited{ 0 };
        uint32_t clustersVisited{ 0 };
        bool done{ false };
    };

    class FatDirectoryReader
    {
    public:
        static constexpr uint32_t maxEntries = 65536;

        FatDirectoryReader(SectorCache& cache, const FatGeometry& geometry, FatEntryAssembler& assembler);
        FatDirectoryReader(const FatDirectoryReader& other) = delete;
        FatDirectoryReader& operator=(const FatDirectoryReader& other) = delete;
        ~FatDirectoryReader();

        FatDirectoryCursor Start(uint32_t firstCluster) const;
        void Read(FatDirectoryCursor& cursor, FatDirectoryVisitor& visitor, const infra::Function<void(Result result)>& onDone);
        uint32_t SectorOf(FatEntryPosition position) const;
        uint32_t Capacity(uint32_t cluster) const;

    private:
        void Resume();
        void ReadSector();
        void OnSector(Result result, infra::ConstByteRange contents);
        bool FeedEntry(infra::ConstByteRange entry);
        void NextSector();
        void OnNextCluster(Result result, std::optional<uint32_t> next);
        void Done(Result result);
        uint32_t EntriesPerSector() const;
        uint32_t EntriesPerCluster() const;
        bool AtEndOfSector() const;
        bool IsFixedRoot() const;

    private:
        SectorCache& cache;
        const FatGeometry& geometry;
        FatEntryAssembler& assembler;
        FatTableReader table;
        FatDirectoryCursor* cursor{ nullptr };
        FatDirectoryVisitor* visitor{ nullptr };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
