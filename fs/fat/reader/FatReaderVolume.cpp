#include "fs/fat/reader/FatReaderVolume.hpp"

namespace fs
{
    FatReaderVolume::FatReaderVolume(hal::BlockDevice& device, SectorSource& sectors, std::size_t transferAlignment, infra::ByteRange nameBuffer, FatEntryAssembler::Reading reading, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved)
        : device(device)
        , deferred(deferred)
        , onMediaRemoved(onMediaRemoved)
        , sectors(sectors)
        , alignment(transferAlignment)
        , assembler(nameBuffer, reading)
    {}

    FatReaderVolume::~FatReaderVolume() = default;

    infra::ClaimableResource& FatReaderVolume::Queue()
    {
        return queue;
    }

    hal::BlockDevice& FatReaderVolume::Device() const
    {
        return device;
    }

    const TransferAlignment& FatReaderVolume::Alignment() const
    {
        return alignment;
    }

    FatGeometry& FatReaderVolume::Geometry()
    {
        return geometry;
    }

    FatTableReader& FatReaderVolume::Table()
    {
        return table;
    }

    FatDirectoryReader& FatReaderVolume::Reader()
    {
        return reader;
    }

    uint32_t FatReaderVolume::Generation() const
    {
        return generation;
    }

    bool FatReaderVolume::Idle() const
    {
        return !queue.IsClaimed() && !queue.ClaimsPending();
    }

    bool FatReaderVolume::Available() const
    {
        return mounted;
    }

    bool FatReaderVolume::Available(uint32_t handleGeneration) const
    {
        return mounted && handleGeneration == generation;
    }

    Result FatReaderVolume::Unavailable() const
    {
        return unavailable;
    }

    void FatReaderVolume::MarkMounted()
    {
        ++generation;
        mounted = true;
    }

    void FatReaderVolume::MarkUnmounted(Result reason)
    {
        mounted = false;
        unavailable = reason == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }

    void FatReaderVolume::Report(Result result) const
    {
        if (result == Result::mediaRemoved)
            onMediaRemoved();
    }

    template void FatReaderVolume::Defer(const infra::Function<void(Result result)>& onDone, Result result);
}
