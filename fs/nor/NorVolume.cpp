#include "fs/nor/NorVolume.hpp"
#include "fs/nor/NorDirectory.hpp"
#include "fs/nor/NorFile.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    NorVolume::NorVolume(const NorVolumeParts& parts, const NorConfig& config, infra::Duration idleDelay)
        : flash(parts.flash)
        , deferred(parts.deferred)
        , config(config)
        , state(parts.blockStates, parts.eraseCounts, parts.nodeBlocks)
        , journalBuffer(parts.journalBuffer)
        , nodeStorage(parts.nodeStorage)
        , cache(parts.flash, parts.nodeSlots, parts.nodeStorage)
        , queue(idleDelay, [this]
              {
                  background.Idle();
              })
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

    NorNameLookup& NorVolume::NameLookup()
    {
        return nameLookup;
    }

    NorBlockEraser& NorVolume::Eraser()
    {
        return eraser;
    }

    NorBlockAllocator& NorVolume::Allocator()
    {
        return allocator;
    }

    NorRecordApply& NorVolume::RecordApply()
    {
        return recordApply;
    }

    NorNodeCollector& NorVolume::Collector()
    {
        return collector;
    }

    NorCheckpointWriter& NorVolume::Checkpoints()
    {
        return checkpoints;
    }

    NorJournalWriter& NorVolume::Journal()
    {
        return journal;
    }

    NorFileFlush& NorVolume::FileFlush()
    {
        return fileFlush;
    }

    NorFileOverwrite& NorVolume::FileOverwrite()
    {
        return fileOverwrite;
    }

    NorFileTruncate& NorVolume::FileTruncate()
    {
        return fileTruncate;
    }

    NorFileCommit& NorVolume::FileCommit()
    {
        return fileCommit;
    }

    infra::IntrusiveList<NorFile>& NorVolume::OpenFiles()
    {
        return openFiles;
    }

    infra::IntrusiveList<NorDirectory>& NorVolume::OpenDirectories()
    {
        return openDirectories;
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
        ++generation;
        mounted = true;
        collector.Reset();
        background.Start();
    }

    void NorVolume::MarkUnmounted()
    {
        mounted = false;
        background.Stop();
        eraser.Reset();

        while (!openFiles.empty())
            openFiles.front().Detach();

        while (!openDirectories.empty())
            openDirectories.front().Detach();
    }
}
