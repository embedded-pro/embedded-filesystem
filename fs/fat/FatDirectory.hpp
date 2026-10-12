#ifndef FS_FAT_FAT_DIRECTORY_HPP
#define FS_FAT_FAT_DIRECTORY_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatVolume.hpp"
#include "fs/interfaces/Directory.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatDirectory
        : public Directory
        , public PooledHandle
        , private FatDirectoryVisitor
    {
    public:
        explicit FatDirectory(FatVolume& volume);
        FatDirectory(const FatDirectory& other) = delete;
        FatDirectory& operator=(const FatDirectory& other) = delete;
        ~FatDirectory();

        void Open(uint32_t firstCluster);
        void Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone) override;

    private:
        void UnReferenced() override;
        bool Visit(const FatScannedEntry& scanned) override;
        void FreeSlot(FatEntryPosition position, bool end) override;

        void Read();
        void Done(Result result);

    private:
        FatVolume& volume;
        OperationQueue::Claimer claimer;
        FatDirectoryCursor cursor;
        uint32_t generation{ 0 };
        std::optional<EntryInfo> entry;
        infra::SharedPtr<FatDirectory> self;
        infra::AutoResetFunction<void(Result result, std::optional<EntryInfo> entry)> onDone;
    };
}

#endif
