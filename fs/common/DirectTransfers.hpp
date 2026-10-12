#ifndef FS_COMMON_DIRECT_TRANSFERS_HPP
#define FS_COMMON_DIRECT_TRANSFERS_HPP

#include "fs/common/FlushBarrier.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class DirectTransfers
    {
    public:
        DirectTransfers(FlushBarrier& barrier, SectorCache& cache);
        DirectTransfers(const DirectTransfers& other) = delete;
        DirectTransfers& operator=(const DirectTransfers& other) = delete;
        ~DirectTransfers() = default;

        void Read(uint32_t firstSector, infra::ByteRange buffer, const infra::Function<void(Result result)>& onDone);
        void Write(uint32_t firstSector, infra::ConstByteRange data, uint8_t rank, const infra::Function<void(Result result)>& onDone);
        void Erase(uint32_t beginSector, uint32_t endSector, const infra::Function<void(Result result)>& onDone);

    private:
        void OnRead(hal::BlockDevice::Result result);
        void OnDone(hal::BlockDevice::Result result);
        uint32_t Sectors(infra::ConstByteRange data) const;

    private:
        FlushBarrier& barrier;
        SectorCache& cache;
        uint32_t firstSector{ 0 };
        infra::ByteRange buffer;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
