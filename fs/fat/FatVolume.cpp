#include "fs/fat/FatVolume.hpp"
#include "fs/fat/FatFile.hpp"

namespace fs
{
    FatVolume::FatVolume(const FatVolumeParts& parts, const FatConfig& config, const infra::Function<void()>& onMediaRemoved)
        : device(parts.device)
        , cache(parts.cache)
        , deferred(parts.deferred)
        , cacheSectors(parts.cacheSectors)
        , limits{ parts.nameBuffer.size(), config.maxPathBytes }
        , onMediaRemoved(onMediaRemoved)
        , transfers(parts.barrier, parts.cache)
        , assembler(parts.nameBuffer)
        , reader(parts.cache, state.geometry, assembler)
        , walk(reader, state.geometry)
        , chain(parts.cache, state.geometry)
        , locator(chain)
        , tableWriter(parts.cache, state.geometry)
        , allocator(parts.cache, state, tableWriter)
        , entryUpdate(parts.cache, reader)
        , closedFiles(parts.closedFiles)
        , freeSpace(parts.cache, state)
        , queue(config.idleDelay, [this]
              {
                  background.Idle();
              })
        , background(queue, freeSpace, [this](Result result)
              {
                  Report(result);
              })
    {}

    FatVolume::~FatVolume() = default;

    hal::BlockDevice& FatVolume::Device() const
    {
        return device;
    }

    SectorCache& FatVolume::Cache() const
    {
        return cache;
    }

    uint32_t FatVolume::CacheSectors() const
    {
        return cacheSectors;
    }

    DirectTransfers& FatVolume::Transfers()
    {
        return transfers;
    }

    PathLimits FatVolume::Limits() const
    {
        return limits;
    }

    FatVolumeState& FatVolume::State()
    {
        return state;
    }

    const FatGeometry& FatVolume::Geometry() const
    {
        return state.geometry;
    }

    FatDirectoryReader& FatVolume::Reader()
    {
        return reader;
    }

    FatPathWalk& FatVolume::Walk()
    {
        return walk;
    }

    FatRunLocator& FatVolume::Locator()
    {
        return locator;
    }

    FatAllocator& FatVolume::Allocator()
    {
        return allocator;
    }

    FatEntryUpdate& FatVolume::EntryUpdate()
    {
        return entryUpdate;
    }

    FatClosedFiles& FatVolume::ClosedFiles()
    {
        return closedFiles;
    }

    FatFreeSpaceScan& FatVolume::FreeSpace()
    {
        return freeSpace;
    }

    infra::IntrusiveList<FatFile>& FatVolume::OpenFiles()
    {
        return openFiles;
    }

    OperationQueue& FatVolume::Queue()
    {
        return queue;
    }

    FatOperation& FatVolume::Operation()
    {
        return operation;
    }

    uint32_t FatVolume::Generation() const
    {
        return generation;
    }

    bool FatVolume::Available() const
    {
        return mounted;
    }

    bool FatVolume::Available(uint32_t handleGeneration) const
    {
        return mounted && handleGeneration == generation;
    }

    Result FatVolume::Unavailable() const
    {
        return unavailable;
    }

    void FatVolume::MarkMounted()
    {
        closedFiles.Clear();
        freeSpace.Restart();
        allocator.Reset();
        ++generation;
        mounted = true;
        background.Start();
    }

    void FatVolume::MarkUnmounted(Result reason)
    {
        mounted = false;
        background.Stop();

        while (!openFiles.empty())
            openFiles.front().Detach();

        unavailable = reason == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }

    void FatVolume::Report(Result result) const
    {
        if (result == Result::mediaRemoved)
            onMediaRemoved();
    }
}
