#ifndef FS_FAT_LITE_FAT_LITE_VOLUME_HPP
#define FS_FAT_LITE_FAT_LITE_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/FlushBarrier.hpp"
#include "fs/fat/FatTableWriter.hpp"
#include "fs/fat/lite/FatLiteAllocator.hpp"
#include "fs/fat/lite/FatLiteWindow.hpp"
#include "fs/fat/reader/FatReaderVolume.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/IntrusiveList.hpp"

namespace fs
{
    class FatLiteFile;

    class FatLiteVolume
        : public FatReaderVolume
    {
    public:
        FatLiteVolume(FlushBarrier& barrier, FatLiteWindow& window, infra::ByteRange nameBuffer, FatEntryAssembler::Reading reading, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved);
        FatLiteVolume(const FatLiteVolume& other) = delete;
        FatLiteVolume& operator=(const FatLiteVolume& other) = delete;
        ~FatLiteVolume();

        FlushBarrier& Barrier() const;
        FatLiteWindow& Window() const;
        FatTableWriter& TableWriter();
        FatLiteAllocator& Allocator();
        infra::IntrusiveList<FatLiteFile>& OpenFiles();
        uint32_t ClusterBytes();

        void EnterMounted();
        void EnterUnmounted(Result reason);

    private:
        FlushBarrier& barrier;
        FatLiteWindow& window;
        FatTableWriter tableWriter;
        FatLiteAllocator allocator;
        infra::IntrusiveList<FatLiteFile> openFiles;
    };
}

#endif
