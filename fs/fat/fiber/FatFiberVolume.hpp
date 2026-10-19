#ifndef FS_FAT_FIBER_FAT_FIBER_VOLUME_HPP
#define FS_FAT_FIBER_FAT_FIBER_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/Deliver.hpp"
#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/fiber/FatFiberWindow.hpp"
#include "fs/fat/fiber/Fiber.hpp"
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
    struct FatEntryStep
    {
        Result result{ Result::success };
        bool found{ false };
    };

    class FatFiberVolume
    {
    public:
        FatFiberVolume(hal::BlockDevice& device, fiber::Runner& runner, FatFiberWindow& window, std::size_t transferAlignment, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved);
        FatFiberVolume(const FatFiberVolume& other) = delete;
        FatFiberVolume& operator=(const FatFiberVolume& other) = delete;
        ~FatFiberVolume() = default;

        infra::ClaimableResource& Queue();
        fiber::Runner& Fiber() const;
        FatFiberWindow& Window() const;
        const FatGeometry& Geometry() const;
        const TransferAlignment& Alignment() const;
        const FatScannedEntry& Entry() const;

        bool FoundRoot() const;
        const FatScannedEntry& Found() const;
        bool FoundDirectory() const;
        uint32_t FoundCluster() const;

        FatDirectoryCursor Start(uint32_t firstCluster) const;
        Result Mount();
        Result NextCluster(uint32_t cluster);
        std::optional<uint32_t> NextClusterFound() const;
        FatEntryStep NextEntry(FatDirectoryCursor& cursor);
        Result Find(infra::BoundedConstString path);

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
        fiber::Runner& runner;
        FatFiberWindow& window;
        DeferredCompletions& deferred;
        infra::Function<void()> onMediaRemoved;
        TransferAlignment alignment;
        FatGeometry geometry;
        FatEntryAssembler assembler;
        std::optional<FatScannedEntry> found;
        std::optional<uint32_t> nextCluster;
        uint32_t generation{ 0 };
        bool mounted{ false };
        Result unavailable{ Result::notMounted };
    };

    template<class Callback>
    void FatFiberVolume::Defer(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                detail::Deliver(onDone, result);
            });
    }
}

#endif
