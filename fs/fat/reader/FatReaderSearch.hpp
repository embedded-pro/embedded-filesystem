#ifndef FS_FAT_READER_FAT_READER_SEARCH_HPP
#define FS_FAT_READER_FAT_READER_SEARCH_HPP

#include "fs/common/Path.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatReaderSearch
        : private FatDirectoryVisitor
    {
    public:
        FatReaderSearch(FatDirectoryReader& reader, const FatGeometry& geometry);
        FatReaderSearch(const FatReaderSearch& other) = delete;
        FatReaderSearch& operator=(const FatReaderSearch& other) = delete;
        ~FatReaderSearch();

        void Find(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone);

        bool FoundRoot() const;
        const FatScannedEntry& Found() const;
        bool FoundDirectory() const;
        uint32_t FoundCluster() const;

        bool LastMissing() const;
        infra::BoundedConstString Component() const;
        const std::optional<FatEntryPosition>& Free() const;
        bool Extensible() const;

    private:
        bool Visit(const FatScannedEntry& entry) override;
        void FreeSlot(FatEntryPosition position, bool end) override;
        void NextComponent();
        void Scanned(Result result);
        void Missed();

    private:
        FatDirectoryReader& reader;
        const FatGeometry& geometry;
        std::optional<PathComponents> components;
        infra::BoundedConstString component;
        FatDirectoryCursor cursor;
        const FatScannedEntry* found{ nullptr };
        std::optional<FatEntryPosition> free;
        bool lastMissing{ false };
        bool extensible{ false };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
