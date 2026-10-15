#ifndef FS_NOR_NOR_VOLUME_HPP
#define FS_NOR_NOR_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/Deliver.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/common/Path.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorBackground.hpp"
#include "fs/nor/NorBlockAllocator.hpp"
#include "fs/nor/NorBlockEraser.hpp"
#include "fs/nor/NorCheckpointWriter.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorDirectoryRead.hpp"
#include "fs/nor/NorEntrySeek.hpp"
#include "fs/nor/NorFileCommit.hpp"
#include "fs/nor/NorFileFlush.hpp"
#include "fs/nor/NorFileOverwrite.hpp"
#include "fs/nor/NorFileTruncate.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorInodeRead.hpp"
#include "fs/nor/NorJournalWriter.hpp"
#include "fs/nor/NorMakeDirectory.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorMount.hpp"
#include "fs/nor/NorNameLookup.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "fs/nor/NorNodeCollector.hpp"
#include "fs/nor/NorOpen.hpp"
#include "fs/nor/NorOpenDirectory.hpp"
#include "fs/nor/NorPathWalk.hpp"
#include "fs/nor/NorRead.hpp"
#include "fs/nor/NorRecordApply.hpp"
#include "fs/nor/NorRemove.hpp"
#include "fs/nor/NorRename.hpp"
#include "fs/nor/NorReplay.hpp"
#include "fs/nor/NorStat.hpp"
#include "fs/nor/NorUnmount.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "fs/nor/NorWrite.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/IntrusiveList.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstdint>
#include <variant>

namespace fs
{
    class NorDirectory;
    class NorFile;

    struct NorVolumeParts
    {
        hal::Flash& flash;
        DeferredCompletions& deferred;
        infra::ByteRange blockStates;
        infra::ByteRange eraseCounts;
        infra::ByteRange nodeBlocks;
        infra::ByteRange journalBuffer;
        infra::ByteRange nodeStorage;
        infra::MemoryRange<NorNodeCache::Slot> nodeSlots;
    };

    using NorOperation = std::variant<std::monostate, NorFormatter, NorMount, NorReplay, NorUnmount, NorStat, NorOpenDirectory, NorOpen, NorRead, NorWrite, NorMakeDirectory, NorRemove, NorRename>;

    class NorVolume
    {
    public:
        NorVolume(const NorVolumeParts& parts, const NorConfig& config, infra::Duration idleDelay);
        NorVolume(const NorVolume& other) = delete;
        NorVolume& operator=(const NorVolume& other) = delete;
        ~NorVolume();

        hal::Flash& Flash() const;
        const NorConfig& Config() const;
        NorVolumeState& State();
        infra::ByteRange JournalBuffer() const;
        infra::ByteRange NodeStorage() const;
        PathLimits Limits() const;
        const NorGeometry& Geometry() const;
        NorNodeCache& Cache();
        NorMapFetch& Fetch();
        NorInodeRead& InodeRead();
        NorPathWalk& Walk();
        NorDirectoryRead& DirectoryRead();
        NorNameLookup& NameLookup();
        NorBlockEraser& Eraser();
        NorBlockAllocator& Allocator();
        NorRecordApply& RecordApply();
        NorNodeCollector& Collector();
        NorCheckpointWriter& Checkpoints();
        NorJournalWriter& Journal();
        NorFileFlush& FileFlush();
        NorFileOverwrite& FileOverwrite();
        NorFileTruncate& FileTruncate();
        NorFileCommit& FileCommit();
        infra::IntrusiveList<NorFile>& OpenFiles();
        infra::IntrusiveList<NorDirectory>& OpenDirectories();
        OperationQueue& Queue();
        NorOperation& Operation();

        uint32_t Generation() const;
        bool Available(uint32_t handleGeneration) const;
        void MarkMounted();
        void MarkUnmounted();

        template<class Callback>
        void Defer(const Callback& onDone, Result result);

    private:
        hal::Flash& flash;
        DeferredCompletions& deferred;
        NorConfig config;
        NorVolumeState state;
        infra::ByteRange journalBuffer;
        infra::ByteRange nodeStorage;
        NorNodeCache cache;
        NorMapFetch fetch{ cache };
        NorInodeRead inodeRead{ cache, fetch, config };
        NorEntrySeek entrySeek{ cache, fetch };
        NorNameLookup nameLookup{ inodeRead, entrySeek };
        NorPathWalk walk{ inodeRead, nameLookup };
        NorDirectoryRead directoryRead{ inodeRead, entrySeek };
        NorBlockEraser eraser{ *this };
        NorBlockAllocator allocator{ *this };
        NorRecordApply recordApply{ *this };
        NorNodeCollector collector{ *this };
        NorCheckpointWriter checkpoints{ *this };
        NorJournalWriter journal{ *this };
        NorFileFlush fileFlush{ *this };
        NorFileOverwrite fileOverwrite{ *this };
        NorFileTruncate fileTruncate{ *this };
        NorFileCommit fileCommit{ *this };
        infra::IntrusiveList<NorFile> openFiles;
        infra::IntrusiveList<NorDirectory> openDirectories;
        OperationQueue queue;
        NorBackground background{ *this };
        NorOperation operation;
        uint32_t generation{ 0 };
        bool mounted{ false };
    };

    template<class Callback>
    void NorVolume::Defer(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                detail::Deliver(onDone, result);
            });
    }
}

#endif
