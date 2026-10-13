#ifndef FS_FAT_FAT_PATH_WALK_HPP
#define FS_FAT_FAT_PATH_WALK_HPP

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
    class FatPathWalk
        : private FatDirectoryVisitor
    {
    public:
        FatPathWalk(FatDirectoryReader& reader, const FatGeometry& geometry);
        FatPathWalk(const FatPathWalk& other) = delete;
        FatPathWalk& operator=(const FatPathWalk& other) = delete;
        ~FatPathWalk();

        void Find(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone);

        bool FoundRoot() const;
        const FatScannedEntry& Found() const;
        bool FoundDirectory() const;
        uint32_t FoundCluster() const;

    private:
        bool Visit(const FatScannedEntry& entry) override;
        void FreeSlot(FatEntryPosition position, bool end) override;

        void NextComponent();
        void OnScanned(Result result);

    private:
        FatDirectoryReader& reader;
        const FatGeometry& geometry;
        std::optional<PathComponents> components;
        infra::BoundedConstString component;
        FatDirectoryCursor cursor;
        std::optional<FatScannedEntry> found;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
