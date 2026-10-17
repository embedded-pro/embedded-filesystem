#ifndef FS_FAT_FAT_ENTRY_UPDATE_HPP
#define FS_FAT_FAT_ENTRY_UPDATE_HPP

#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatOpenFile.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstdint>

namespace fs
{
    class FatVolume;

    class FatEntryUpdate
    {
    public:
        FatEntryUpdate(SectorCache& cache, FatDirectoryReader& reader, const FatVolume& volume);
        FatEntryUpdate(const FatEntryUpdate& other) = delete;
        FatEntryUpdate& operator=(const FatEntryUpdate& other) = delete;
        ~FatEntryUpdate();

        void Commit(FatOpenFile& file, const infra::Function<void(Result result)>& onDone);

    private:
        void OnSector(Result result, infra::ConstByteRange contents);
        void Done(Result result);
        uint32_t Offset() const;

    private:
        SectorCache& cache;
        FatDirectoryReader& reader;
        const FatVolume& volume;
        FatOpenFile* file{ nullptr };
        std::array<uint8_t, directoryEntryBytes> updated{};
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
