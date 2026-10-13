#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatVolume::FatVolume(const FatVolumeParts& parts, const FatConfig& config, const infra::Function<void()>& onMediaRemoved)
        : device(parts.device)
        , cache(parts.cache)
        , deferred(parts.deferred)
        , limits{ parts.nameBuffer.size(), config.maxPathBytes }
        , onMediaRemoved(onMediaRemoved)
        , transfers(parts.barrier, parts.cache)
        , assembler(parts.nameBuffer)
        , reader(parts.cache, state.geometry, assembler)
        , walk(reader, state.geometry)
        , chain(parts.cache, state.geometry)
        , closedFiles(parts.closedFiles)
        , queue(config.idleDelay, [] {})
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

    FatChainWalk& FatVolume::Chain()
    {
        return chain;
    }

    FatClosedFiles& FatVolume::ClosedFiles()
    {
        return closedFiles;
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
        ++generation;
        mounted = true;
    }

    void FatVolume::MarkUnmounted(Result reason)
    {
        mounted = false;
        unavailable = reason == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }

    void FatVolume::Report(Result result) const
    {
        if (result == Result::mediaRemoved)
            onMediaRemoved();
    }
}
