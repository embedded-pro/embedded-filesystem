#ifndef FS_FAT_FAT_VOLUME_HPP
#define FS_FAT_FAT_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/DirectTransfers.hpp"
#include "fs/common/FlushBarrier.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/common/Path.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatAllocator.hpp"
#include "fs/fat/FatBackgroundScan.hpp"
#include "fs/fat/FatChainFree.hpp"
#include "fs/fat/FatChainWalk.hpp"
#include "fs/fat/FatClosedFiles.hpp"
#include "fs/fat/FatConfig.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatEntryUpdate.hpp"
#include "fs/fat/FatExpectSize.hpp"
#include "fs/fat/FatFreeSpaceScan.hpp"
#include "fs/fat/FatMakeDirectory.hpp"
#include "fs/fat/FatMount.hpp"
#include "fs/fat/FatOpen.hpp"
#include "fs/fat/FatOpenDirectory.hpp"
#include "fs/fat/FatPathWalk.hpp"
#include "fs/fat/FatRead.hpp"
#include "fs/fat/FatRemove.hpp"
#include "fs/fat/FatRunLocator.hpp"
#include "fs/fat/FatStat.hpp"
#include "fs/fat/FatSync.hpp"
#include "fs/fat/FatTableWriter.hpp"
#include "fs/fat/FatTimestamp.hpp"
#include "fs/fat/FatUnmount.hpp"
#include "fs/fat/FatUsage.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/fat/FatWrite.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/IntrusiveList.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace fs
{
    namespace detail
    {
        inline void Deliver(const infra::Function<void(Result result)>& onDone, Result result)
        {
            onDone(result);
        }

        inline void Deliver(const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone, Result result)
        {
            onDone(result, nullptr);
        }

        inline void Deliver(const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone, Result result)
        {
            onDone(result, nullptr);
        }

        inline void Deliver(const infra::Function<void(Result result, const EntryInfo& info)>& onDone, Result result)
        {
            onDone(result, EntryInfo{});
        }

        inline void Deliver(const infra::Function<void(Result result, VolumeUsage usage)>& onDone, Result result)
        {
            onDone(result, VolumeUsage{});
        }

        inline void Deliver(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone, Result result)
        {
            onDone(result, std::nullopt);
        }

        inline void Deliver(const infra::Function<void(Result result, std::size_t bytesRead)>& onDone, Result result)
        {
            onDone(result, 0);
        }
    }

    class FatDirectory;
    class FatFile;

    using FatOperation = std::variant<std::monostate, FatMount, FatUnmount, FatStat, FatOpenDirectory, FatOpen, FatMakeDirectory, FatRemove, FatRead, FatWrite, FatSync, FatExpectSize, FatUsage>;

    struct FatVolumeParts
    {
        hal::BlockDevice& device;
        FlushBarrier& barrier;
        SectorCache& cache;
        DeferredCompletions& deferred;
        infra::ByteRange nameBuffer;
        infra::MemoryRange<FatClosedFile> closedFiles;
        uint32_t cacheSectors;
    };

    class FatVolume
        : private FatReservations
    {
    public:
        FatVolume(const FatVolumeParts& parts, const FatConfig& config, const infra::Function<void()>& onMediaRemoved);
        FatVolume(const FatVolume& other) = delete;
        FatVolume& operator=(const FatVolume& other) = delete;
        ~FatVolume();

        hal::BlockDevice& Device() const;
        SectorCache& Cache() const;
        uint32_t CacheSectors() const;
        DirectTransfers& Transfers();
        PathLimits Limits() const;
        FatVolumeState& State();
        const FatGeometry& Geometry() const;
        FatDirectoryReader& Reader();
        FatPathWalk& Walk();
        FatRunLocator& Locator();
        FatAllocator& Allocator();
        FatEntryUpdate& EntryUpdate();
        FatChainFree& ChainFree();
        FatClosedFiles& ClosedFiles();
        FatFreeSpaceScan& FreeSpace();
        infra::IntrusiveList<FatFile>& OpenFiles();
        infra::IntrusiveList<FatDirectory>& OpenDirectories();
        OperationQueue& Queue();
        FatOperation& Operation();
        const FatReservations& Reservations() const;
        FatDateTime Timestamp() const;

        uint32_t Generation() const;
        bool Available() const;
        bool Available(uint32_t handleGeneration) const;
        Result Unavailable() const;
        void MarkMounted();
        void MarkUnmounted(Result reason);
        void Report(Result result);

        template<class Callback>
        void Defer(const Callback& onDone, Result result);

    private:
        bool Reserved(uint32_t cluster) const override;

    private:
        hal::BlockDevice& device;
        SectorCache& cache;
        DeferredCompletions& deferred;
        uint32_t cacheSectors;
        uint32_t timerServiceId;
        uint32_t blocksPerAllocationUnit;
        PathLimits limits;
        infra::Function<void()> onMediaRemoved;
        DirectTransfers transfers;
        FatVolumeState state;
        FatEntryAssembler assembler;
        FatDirectoryReader reader;
        FatPathWalk walk;
        FatChainWalk chain;
        FatRunLocator locator;
        FatTableWriter tableWriter;
        FatAllocator allocator;
        FatEntryUpdate entryUpdate;
        FatChainFree chainFree;
        FatClosedFiles closedFiles;
        infra::IntrusiveList<FatFile> openFiles;
        infra::IntrusiveList<FatDirectory> openDirectories;
        FatFreeSpaceScan freeSpace;
        OperationQueue queue;
        FatBackgroundScan background;
        FatOperation operation;
        uint32_t generation{ 0 };
        bool mounted{ false };
        Result unavailable{ Result::notMounted };
    };

    template<class Callback>
    void FatVolume::Defer(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                detail::Deliver(onDone, result);
            });
    }
}

#endif
