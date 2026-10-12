#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatVolume::FatVolume(const FatVolumeParts& parts, const FatConfig& config, const infra::Function<void()>& onMediaRemoved)
        : device(parts.device)
        , cache(parts.cache)
        , deferred(parts.deferred)
        , limits{ parts.nameBuffer.size(), config.maxPathBytes }
        , onMediaRemoved(onMediaRemoved)
        , assembler(parts.nameBuffer)
        , reader(parts.cache, state.geometry, assembler)
        , walk(reader, state.geometry)
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

    PathLimits FatVolume::Limits() const
    {
        return limits;
    }

    FatVolumeState& FatVolume::State()
    {
        return state;
    }

    FatDirectoryReader& FatVolume::Reader()
    {
        return reader;
    }

    FatPathWalk& FatVolume::Walk()
    {
        return walk;
    }

    OperationQueue& FatVolume::Queue()
    {
        return queue;
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
        ++generation;
        mounted = true;
    }

    void FatVolume::MarkUnmounted(Result reason)
    {
        mounted = false;
        unavailable = reason == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }

    void FatVolume::Report(Result result)
    {
        if (result == Result::mediaRemoved)
            onMediaRemoved();
    }
}
