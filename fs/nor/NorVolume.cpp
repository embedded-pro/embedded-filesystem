#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    NorVolume::NorVolume(const NorVolumeParts& parts, const NorConfig& config, infra::Duration idleDelay)
        : flash(parts.flash)
        , deferred(parts.deferred)
        , config(config)
        , state(parts.blockStates, parts.eraseCounts)
        , journalBuffer(parts.journalBuffer)
        , nodeStorage(parts.nodeStorage)
        , cache(parts.flash, parts.nodeSlots, parts.nodeStorage)
        , queue(idleDelay, [] {})
    {}

    NorVolume::~NorVolume() = default;

    hal::Flash& NorVolume::Flash() const
    {
        return flash;
    }

    const NorConfig& NorVolume::Config() const
    {
        return config;
    }

    NorVolumeState& NorVolume::State()
    {
        return state;
    }

    infra::ByteRange NorVolume::JournalBuffer() const
    {
        return journalBuffer;
    }

    infra::ByteRange NorVolume::NodeStorage() const
    {
        return nodeStorage;
    }

    PathLimits NorVolume::Limits() const
    {
        return PathLimits{ config.maxNameBytes, config.maxPathBytes };
    }

    const NorGeometry& NorVolume::Geometry() const
    {
        really_assert(state.geometry.has_value());
        return *state.geometry;
    }

    NorNodeCache& NorVolume::Cache()
    {
        return cache;
    }

    NorMapFetch& NorVolume::Fetch()
    {
        return fetch;
    }

    NorInodeRead& NorVolume::InodeRead()
    {
        return inodeRead;
    }

    NorPathWalk& NorVolume::Walk()
    {
        return walk;
    }

    NorDirectoryRead& NorVolume::DirectoryRead()
    {
        return directoryRead;
    }

    OperationQueue& NorVolume::Queue()
    {
        return queue;
    }

    NorOperation& NorVolume::Operation()
    {
        return operation;
    }

    uint32_t NorVolume::Generation() const
    {
        return generation;
    }

    bool NorVolume::Available(uint32_t handleGeneration) const
    {
        return mounted && handleGeneration == generation;
    }

    void NorVolume::MarkMounted()
    {
        cache.Clear();
        ++generation;
        mounted = true;
    }

    void NorVolume::MarkUnmounted()
    {
        mounted = false;
    }
}
