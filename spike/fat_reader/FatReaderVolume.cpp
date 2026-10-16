#include "spike/fat_reader/FatReaderVolume.hpp"

namespace spike
{
    FatReaderVolume::FatReaderVolume(hal::BlockDevice& device, infra::ByteRange sector, fs::DeferredCompletions& deferred)
        : deferred(deferred)
        , window(device, sector)
    {}

    infra::ClaimableResource& FatReaderVolume::Queue()
    {
        return queue;
    }

    FatLayout& FatReaderVolume::Layout()
    {
        return layout;
    }

    FatWindow& FatReaderVolume::Window()
    {
        return window;
    }

    FatChain& FatReaderVolume::Chain()
    {
        return chain;
    }

    FatScan& FatReaderVolume::Scan()
    {
        return scan;
    }

    FatWalk& FatReaderVolume::Walk()
    {
        return walk;
    }

    uint32_t FatReaderVolume::Generation() const
    {
        return generation;
    }

    bool FatReaderVolume::Available(uint32_t handleGeneration) const
    {
        return mounted && handleGeneration == generation;
    }

    void FatReaderVolume::MarkMounted()
    {
        mounted = true;
    }

    void FatReaderVolume::MarkUnmounted()
    {
        mounted = false;
        ++generation;
        window.Forget();
    }
}
