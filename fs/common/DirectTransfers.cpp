#include "fs/common/DirectTransfers.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    DirectTransfers::DirectTransfers(FlushBarrier& barrier, SectorCache& cache)
        : barrier(barrier)
        , cache(cache)
    {}

    DirectTransfers::~DirectTransfers() = default;

    void DirectTransfers::Read(uint32_t firstSector, infra::ByteRange buffer, const infra::Function<void(Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->firstSector = firstSector;
        this->buffer = buffer;
        this->onDone = onDone;
        barrier.Device().ReadBlocks(buffer, firstSector, [this](hal::BlockDevice::Result result)
            {
                OnRead(result);
            });
    }

    void DirectTransfers::Write(uint32_t firstSector, infra::ConstByteRange data, uint8_t rank, const infra::Function<void(Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->onDone = onDone;
        cache.Discard(firstSector, firstSector + Sectors(data));
        barrier.Write(data, firstSector, rank, [this](hal::BlockDevice::Result result)
            {
                OnDone(result);
            });
    }

    void DirectTransfers::Erase(uint32_t beginSector, uint32_t endSector, const infra::Function<void(Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->onDone = onDone;
        cache.Discard(beginSector, endSector);
        barrier.Device().EraseBlocks(beginSector, endSector, [this](hal::BlockDevice::Result result)
            {
                OnDone(result);
            });
    }

    void DirectTransfers::OnRead(hal::BlockDevice::Result result)
    {
        if (result == hal::BlockDevice::Result::success)
            cache.Overlay(firstSector, buffer);

        OnDone(result);
    }

    void DirectTransfers::OnDone(hal::BlockDevice::Result result)
    {
        onDone(ToResult(result));
    }

    uint32_t DirectTransfers::Sectors(infra::ConstByteRange data) const
    {
        really_assert(data.size() % cache.SectorSize() == 0);

        return static_cast<uint32_t>(data.size() / cache.SectorSize());
    }
}
