#ifndef FS_COMMON_FLUSH_BARRIER_HPP
#define FS_COMMON_FLUSH_BARRIER_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FlushBarrier
    {
    public:
        explicit FlushBarrier(hal::BlockDevice& device);
        FlushBarrier(const FlushBarrier& other) = delete;
        FlushBarrier& operator=(const FlushBarrier& other) = delete;
        ~FlushBarrier();

        hal::BlockDevice& Device() const;
        bool HasUnflushedWrites() const;

        void Write(infra::ConstByteRange data, uint32_t firstBlock, uint8_t rank, const infra::Function<void(hal::BlockDevice::Result result)>& onDone);
        void Flush(const infra::Function<void(hal::BlockDevice::Result result)>& onDone);

    private:
        enum class Step : uint8_t
        {
            write,
            flush
        };

        void Transfer(Step next);
        void Transferred(hal::BlockDevice::Result result);
        void OnWritten(hal::BlockDevice::Result result);
        void OnFlushed(hal::BlockDevice::Result result);

    private:
        hal::BlockDevice& device;
        std::optional<uint8_t> lowestUnflushedRank;
        infra::ConstByteRange data;
        uint32_t firstBlock{ 0 };
        uint8_t rank{ 0 };
        bool writeAfterFlush{ false };
        Step step{ Step::write };
        infra::AutoResetFunction<void(hal::BlockDevice::Result result)> onDone;
    };
}

#endif
