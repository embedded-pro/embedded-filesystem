#ifndef FS_FAT_READER_FAT_READER_VOLUME_HPP
#define FS_FAT_READER_FAT_READER_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/Deliver.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatPathWalk.hpp"
#include "fs/fat/FatTableReader.hpp"
#include "fs/fat/reader/FatReaderWindow.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatReaderVolume
    {
    public:
        FatReaderVolume(hal::BlockDevice& device, infra::ByteRange sector, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved);
        FatReaderVolume(const FatReaderVolume& other) = delete;
        FatReaderVolume& operator=(const FatReaderVolume& other) = delete;
        ~FatReaderVolume();

        infra::ClaimableResource& Queue();
        hal::BlockDevice& Device() const;
        FatReaderWindow& Window();
        FatGeometry& Geometry();
        FatTableReader& Table();
        FatDirectoryReader& Reader();
        FatPathWalk& Walk();

        uint32_t Generation() const;
        bool Available() const;
        bool Available(uint32_t handleGeneration) const;
        Result Unavailable() const;
        void MarkMounted();
        void MarkUnmounted(Result reason);
        void Report(Result result) const;

        template<class Callback>
        void Defer(const Callback& onDone, Result result);

    private:
        infra::ClaimableResource queue;
        hal::BlockDevice& device;
        DeferredCompletions& deferred;
        infra::Function<void()> onMediaRemoved;
        FatGeometry geometry;
        FatReaderWindow window;
        FatTableReader table{ window, geometry };
        FatEntryAssembler assembler;
        FatDirectoryReader reader{ window, geometry, assembler };
        FatPathWalk walk{ reader, geometry };
        uint32_t generation{ 0 };
        bool mounted{ false };
        Result unavailable{ Result::notMounted };
    };

    template<class Callback>
    void FatReaderVolume::Defer(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                detail::Deliver(onDone, result);
            });
    }
}

#endif
