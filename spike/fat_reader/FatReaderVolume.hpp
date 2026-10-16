#ifndef SPIKE_FAT_READER_FAT_READER_VOLUME_HPP
#define SPIKE_FAT_READER_FAT_READER_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/Deliver.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/ByteRange.hpp"
#include "spike/fat_reader/FatReaderLayout.hpp"
#include "spike/fat_reader/FatReaderScan.hpp"
#include "spike/fat_reader/FatReaderWalk.hpp"
#include "spike/fat_reader/FatReaderWindow.hpp"
#include <cstdint>

namespace spike
{
    class FatReaderVolume
    {
    public:
        FatReaderVolume(hal::BlockDevice& device, infra::ByteRange sector, fs::DeferredCompletions& deferred);
        FatReaderVolume(const FatReaderVolume& other) = delete;
        FatReaderVolume& operator=(const FatReaderVolume& other) = delete;
        ~FatReaderVolume() = default;

        infra::ClaimableResource& Queue();
        FatLayout& Layout();
        FatWindow& Window();
        FatChain& Chain();
        FatScan& Scan();
        FatWalk& Walk();

        uint32_t Generation() const;
        bool Available(uint32_t handleGeneration) const;
        void MarkMounted();
        void MarkUnmounted();

        template<class Callback>
        void Defer(const Callback& onDone, fs::Result result);

    private:
        infra::ClaimableResource queue;
        fs::DeferredCompletions& deferred;
        FatLayout layout;
        FatWindow window;
        FatChain chain{ layout, window };
        FatScan scan{ layout, window, chain };
        FatWalk walk{ layout, scan };
        uint32_t generation{ 0 };
        bool mounted{ false };
    };

    template<class Callback>
    void FatReaderVolume::Defer(const Callback& onDone, fs::Result result)
    {
        deferred.Defer([onDone, result]
            {
                fs::detail::Deliver(onDone, result);
            });
    }
}

#endif
