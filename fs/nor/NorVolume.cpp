#include "fs/nor/NorVolume.hpp"
#include "fs/nor/NorDirectory.hpp"
#include "fs/nor/NorFile.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    NorVolume::NorVolume(const NorVolumeParts& parts, const NorConfig& config)
        : flash(parts.flash)
        , deferred(parts.deferred)
        , operations(parts.operations)
        , config(config)
        , state(parts.blockStates, parts.eraseCounts, parts.nodeBlocks)
        , journalBuffer(parts.journalBuffer)
        , nodeStorage(parts.nodeStorage)
        , cache(parts.flash, parts.nodeSlots, parts.nodeStorage)
        , queue(
              [this]
              {
                  if (background != nullptr)
                      background->ForegroundClaiming();
              },
              [this]
              {
                  if (background != nullptr)
                      background->ForegroundReleased();
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

    NorCounters& NorVolume::Counters()
    {
        return counters;
    }

    const NorCounters& NorVolume::Counters() const
    {
        return counters;
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

    NorDataAllocator& NorVolume::DataAllocator()
    {
        return dataAllocator;
    }

    NorRecordApply& NorVolume::RecordApply()
    {
        return recordApply;
    }

    NorNodeCollector& NorVolume::Collector()
    {
        return collector;
    }

    NorWearLeveler& NorVolume::Leveler()
    {
        return leveler;
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

    const NorOperations& NorVolume::Operations() const
    {
        return operations;
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
        counters = NorCounters{};
        collector.Reset();
        leveler.Reset();
        if (background != nullptr)
            background->Start();
    }

    void NorVolume::MarkUnmounted()
    {
        mounted = false;
        if (background != nullptr)
            background->Stop();
        eraser.Reset();

        while (!openFiles.empty())
            openFiles.front().Detach();

        while (!openDirectories.empty())
            openDirectories.front().Detach();
    }

    void NorVolume::AttachBackground(NorBackgroundWork& work)
    {
        background = &work;
    }

    void NorVolume::UrgeBackground()
    {
        if (background != nullptr)
            background->Urge();
    }
}
