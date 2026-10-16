#include "fs/fat/lite/FatLiteVolume.hpp"
#include "fs/fat/lite/FatLiteFile.hpp"

namespace fs
{
    FatLiteVolume::FatLiteVolume(FlushBarrier& barrier, FatLiteWindow& window, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved)
        : FatReaderVolume(barrier.Device(), window, nameBuffer, deferred, onMediaRemoved)
        , barrier(barrier)
        , window(window)
        , tableWriter(window, Geometry())
        , allocator(*this)
    {}

    FatLiteVolume::~FatLiteVolume() = default;

    FlushBarrier& FatLiteVolume::Barrier() const
    {
        return barrier;
    }

    FatLiteWindow& FatLiteVolume::Window() const
    {
        return window;
    }

    FatTableWriter& FatLiteVolume::TableWriter()
    {
        return tableWriter;
    }

    FatLiteAllocator& FatLiteVolume::Allocator()
    {
        return allocator;
    }

    infra::IntrusiveList<FatLiteFile>& FatLiteVolume::OpenFiles()
    {
        return openFiles;
    }

    uint32_t FatLiteVolume::ClusterBytes()
    {
        return Geometry().sectorsPerCluster * window.SectorSize();
    }

    void FatLiteVolume::MarkMounted()
    {
        auto& geometry = Geometry();

        if (geometry.mirrored && geometry.fatCopies > 1)
            window.SetMirroredRegion(SectorCache::MirroredRegion{ geometry.firstFatSector, geometry.sectorsPerFat, geometry.fatCopies });
        else
            window.SetMirroredRegion(std::nullopt);

        allocator.Reset();
        FatReaderVolume::MarkMounted();
    }

    void FatLiteVolume::MarkUnmounted(Result reason)
    {
        FatReaderVolume::MarkUnmounted(reason);

        while (!openFiles.empty())
            openFiles.front().Detach();
    }
}
