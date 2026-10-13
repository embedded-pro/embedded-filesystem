#ifndef FS_FAT_FAT_VOLUME_HPP
#define FS_FAT_FAT_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/common/Path.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatConfig.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatPathWalk.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

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
    }

    struct FatVolumeParts
    {
        hal::BlockDevice& device;
        SectorCache& cache;
        DeferredCompletions& deferred;
        infra::ByteRange nameBuffer;
    };

    class FatVolume
    {
    public:
        FatVolume(const FatVolumeParts& parts, const FatConfig& config, const infra::Function<void()>& onMediaRemoved);
        FatVolume(const FatVolume& other) = delete;
        FatVolume& operator=(const FatVolume& other) = delete;
        ~FatVolume();

        hal::BlockDevice& Device() const;
        SectorCache& Cache() const;
        PathLimits Limits() const;
        FatVolumeState& State();
        FatDirectoryReader& Reader();
        FatPathWalk& Walk();
        OperationQueue& Queue();

        uint32_t Generation() const;
        bool Available() const;
        bool Available(uint32_t handleGeneration) const;
        Result Unavailable() const;
        void MarkMounted();
        void MarkUnmounted(Result reason);
        void Report(Result result);

        template<class Callback>
        void Reject(const Callback& onDone, Result result);

    private:
        hal::BlockDevice& device;
        SectorCache& cache;
        DeferredCompletions& deferred;
        PathLimits limits;
        infra::Function<void()> onMediaRemoved;
        FatVolumeState state;
        FatEntryAssembler assembler;
        FatDirectoryReader reader;
        FatPathWalk walk;
        OperationQueue queue;
        uint32_t generation{ 0 };
        bool mounted{ false };
        Result unavailable{ Result::notMounted };
    };

    template<class Callback>
    void FatVolume::Reject(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                detail::Deliver(onDone, result);
            });
    }
}

#endif
