#ifndef FS_FAT_CORO_FAT_CORO_VOLUME_HPP
#define FS_FAT_CORO_FAT_CORO_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/Deliver.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/coro/Coroutine.hpp"
#include "fs/fat/coro/FatCoroWindow.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatClusterStep
    {
        Result result{ Result::success };
        std::optional<uint32_t> next;
    };

    struct FatEntryStep
    {
        Result result{ Result::success };
        bool found{ false };
    };

    class FatCoroVolume
    {
    public:
        FatCoroVolume(hal::BlockDevice& device, FatCoroWindow& window, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved);
        FatCoroVolume(const FatCoroVolume& other) = delete;
        FatCoroVolume& operator=(const FatCoroVolume& other) = delete;
        ~FatCoroVolume() = default;

        infra::ClaimableResource& Queue();
        hal::BlockDevice& Device() const;
        FatCoroWindow& Window() const;
        const FatGeometry& Geometry() const;
        const FatScannedEntry& Entry() const;

        bool FoundRoot() const;
        const FatScannedEntry& Found() const;
        bool FoundDirectory() const;
        uint32_t FoundCluster() const;

        FatDirectoryCursor Start(uint32_t firstCluster) const;
        coro::Task<Result> Mount(coro::Frames& frames);
        coro::Task<Result> NextCluster(coro::Frames& frames, uint32_t cluster);
        std::optional<uint32_t> NextClusterFound() const;
        coro::Task<FatEntryStep> NextEntry(coro::Frames& frames, FatDirectoryCursor& cursor);
        coro::Task<Result> Find(coro::Frames& frames, infra::BoundedConstString path);

        uint32_t Generation() const;
        bool Idle() const;
        bool Available() const;
        bool Available(uint32_t handleGeneration) const;
        Result Unavailable() const;
        void MarkMounted();
        void MarkUnmounted(Result reason);
        void Report(Result result) const;

        template<class Callback>
        void Defer(const Callback& onDone, Result result);

    private:
        Result UseGeometry(infra::ConstByteRange bootSector, uint32_t start, uint32_t sectors);
        uint32_t EntriesPerSector() const;
        uint32_t EntriesPerCluster() const;
        uint32_t SectorOf(const FatDirectoryCursor& cursor) const;

    private:
        infra::ClaimableResource queue;
        hal::BlockDevice& device;
        FatCoroWindow& window;
        DeferredCompletions& deferred;
        infra::Function<void()> onMediaRemoved;
        FatGeometry geometry;
        FatEntryAssembler assembler;
        std::optional<FatScannedEntry> found;
        std::optional<uint32_t> nextCluster;
        uint32_t generation{ 0 };
        bool mounted{ false };
        Result unavailable{ Result::notMounted };
    };

    template<class Callback>
    void FatCoroVolume::Defer(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                detail::Deliver(onDone, result);
            });
    }
}

#endif
