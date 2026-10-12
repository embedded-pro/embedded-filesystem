#ifndef FS_FAT_FAT_CLOSED_FILES_HPP
#define FS_FAT_FAT_CLOSED_FILES_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatExtentCache.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatClosedFile
    {
        FatEntryPosition entry{};
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        FatExtent last;
    };

    class FatClosedFiles
    {
    public:
        explicit FatClosedFiles(infra::MemoryRange<FatClosedFile> storage);
        FatClosedFiles(const FatClosedFiles& other) = delete;
        FatClosedFiles& operator=(const FatClosedFiles& other) = delete;
        ~FatClosedFiles();

        void Clear();
        void Remember(const FatClosedFile& file);
        void Forget(FatEntryPosition entry);
        std::optional<FatExtent> Recall(FatEntryPosition entry, uint32_t firstCluster, uint32_t size) const;

    private:
        infra::MemoryRange<FatClosedFile> storage;
        std::size_t count{ 0 };
    };
}

#endif
