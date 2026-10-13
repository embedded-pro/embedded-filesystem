#include "fs/fat/FatVolume.hpp"
#include "fs/fat/FatDirectory.hpp"
#include "fs/fat/FatFile.hpp"
#include <algorithm>

namespace fs
{
    FatVolume::FatVolume(const FatVolumeParts& parts, const FatConfig& config, const infra::Function<void()>& onMediaRemoved)
        : device(parts.device)
        , cache(parts.cache)
        , deferred(parts.deferred)
        , cacheSectors(parts.cacheSectors)
        , timerServiceId(config.timerServiceId)
        , blocksPerAllocationUnit(config.blocksPerAllocationUnit)
        , limits{ parts.nameBuffer.size(), config.maxPathBytes }
        , onMediaRemoved(onMediaRemoved)
        , transfers(parts.barrier, parts.cache)
        , assembler(parts.nameBuffer)
        , reader(parts.cache, state.geometry, assembler)
        , walk(reader, state.geometry)
        , chain(parts.cache, state.geometry)
        , locator(chain)
        , tableWriter(parts.cache, state.geometry)
        , allocator(parts.cache, state, tableWriter, *this)
        , entryUpdate(parts.cache, reader, config.timerServiceId)
        , chainFree(parts.cache, transfers, state, tableWriter, config.discard)
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

    FatChainFree& FatVolume::ChainFree()
    {
        return chainFree;
    }

    FatTableWriter& FatVolume::TableWriter()
    {
        return tableWriter;
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

    infra::IntrusiveList<FatDirectory>& FatVolume::OpenDirectories()
    {
        return openDirectories;
    }

    OperationQueue& FatVolume::Queue()
    {
        return queue;
    }

    FatOperation& FatVolume::Operation()
    {
        return operation;
    }

    const FatReservations& FatVolume::Reservations() const
    {
        return *this;
    }

    FatDateTime FatVolume::Timestamp() const
    {
        return ToFatDateTime(infra::Now(timerServiceId));
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
        state.allocationUnits = AllocationUnitsOf(state.geometry, blocksPerAllocationUnit);
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

        while (!openDirectories.empty())
            openDirectories.front().Detach();

        unavailable = reason == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }

    bool FatVolume::Reserved(uint32_t cluster) const
    {
        return std::ranges::any_of(openFiles, [cluster](const FatFile& file)
            {
                return file.State().reservation.Contains(cluster);
            });
    }

    void FatVolume::Report(Result result) const
    {
        if (result == Result::mediaRemoved)
            onMediaRemoved();
    }
}
