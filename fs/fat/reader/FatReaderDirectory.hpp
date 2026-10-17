#ifndef FS_FAT_READER_FAT_READER_DIRECTORY_HPP
#define FS_FAT_READER_FAT_READER_DIRECTORY_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/reader/FatReaderVolume.hpp"
#include "fs/interfaces/Directory.hpp"
#include "fs/interfaces/EntryInfo.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatReaderDirectory
        : public Directory
        , public PooledHandle
        , private FatDirectoryVisitor
    {
    public:
        explicit FatReaderDirectory(FatReaderVolume& volume);
        FatReaderDirectory(const FatReaderDirectory& other) = delete;
        FatReaderDirectory& operator=(const FatReaderDirectory& other) = delete;
        ~FatReaderDirectory() = default;

        void Open(uint32_t firstCluster);
        void Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone) override;

    private:
        void UnReferenced() override;
        bool Visit(const FatScannedEntry& scanned) override;
        void FreeSlot(FatEntryPosition position, bool end) override;
        void Read();
        void Done(Result result);
        void Finish(Result result);

    private:
        FatReaderVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        uint32_t generation{ 0 };
        FatDirectoryCursor cursor;
        std::optional<EntryInfo> entry;
        infra::SharedPtr<FatReaderDirectory> self;
        infra::AutoResetFunction<void(Result result, std::optional<EntryInfo> entry)> onDone;
    };
}

#endif
