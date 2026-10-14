#include "fs/nor/NorVolume.hpp"

namespace fs
{
    NorVolume::NorVolume(const NorVolumeParts& parts, const NorConfig& config, infra::Duration idleDelay)
        : flash(parts.flash)
        , deferred(parts.deferred)
        , config(config)
        , state(parts.blockStates, parts.eraseCounts)
        , journalBuffer(parts.journalBuffer)
        , nodeStorage(parts.nodeStorage)
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

    OperationQueue& NorVolume::Queue()
    {
        return queue;
    }

    NorOperation& NorVolume::Operation()
    {
        return operation;
    }
}
