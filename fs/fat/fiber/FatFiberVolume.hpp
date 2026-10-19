#ifndef FS_FAT_FIBER_FAT_FIBER_VOLUME_HPP
#define FS_FAT_FIBER_FAT_FIBER_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/Deliver.hpp"
#include "fs/common/Fiber.hpp"
#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
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
        FatFiberVolume(hal::BlockDevice& device, infra::ByteRange stack, infra::ByteRange sector, std::size_t transferAlignment, infra::ByteRange nameBuffer, FatEntryAssembler::Reading reading, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved);
        FatFiberVolume(const FatFiberVolume& other) = delete;
        FatFiberVolume& operator=(const FatFiberVolume& other) = delete;
        ~FatFiberVolume();

        infra::ClaimableResource& Queue();
        void Run(infra::ClaimableResource::Claimer& claimer, const infra::Function<void()>& body);
        std::size_t StackPeak() const;
        const FatGeometry& Geometry() const;
        const TransferAlignment& Alignment() const;
        const FatScannedEntry& Entry() const;

        Result ReadBlocks(infra::ByteRange buffer, uint32_t block);
        Result ReadSector(uint32_t sector);
        infra::ConstByteRange Sector() const;
        uint32_t SectorSize() const;

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
        static constexpr uint32_t noSector = std::numeric_limits<uint32_t>::max();

        Result UseGeometry(uint32_t start, uint32_t sectors);
        uint32_t EntriesPerSector() const;
        uint32_t EntriesPerCluster() const;
        uint32_t SectorOf(const FatDirectoryCursor& cursor) const;

    private:
        infra::ClaimableResource queue;
        hal::BlockDevice& device;
        Fiber fiber;
        infra::ByteRange sector;
        DeferredCompletions& deferred;
        infra::Function<void()> onMediaRemoved;
        TransferAlignment alignment;
        FatGeometry geometry;
        FatEntryAssembler assembler;
        std::optional<FatScannedEntry> found;
        std::optional<uint32_t> nextCluster;
        uint32_t loaded{ noSector };
        uint32_t generation{ 0 };
        bool mounted{ false };
        Result unavailable{ Result::notMounted };
        hal::BlockDevice::Result deviceResult{ hal::BlockDevice::Result::success };
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
