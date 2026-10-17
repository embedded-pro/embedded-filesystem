#include "fs/fat/coro/Coroutine.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs::coro
{
    namespace
    {
        constexpr std::size_t header = alignof(std::max_align_t);

        constexpr std::size_t Footprint(std::size_t size)
        {
            return header + (size + header - 1) / header * header;
        }
    }

    Frames::Frames(infra::ByteRange storage)
        : storage(storage)
    {}

    void* Frames::Allocate(std::size_t size)
    {
        auto bytes = Footprint(size);
        really_assert(used + bytes <= storage.size());

        auto* block = storage.begin() + used;
        used += bytes;
        peak = std::max(peak, used);
        *reinterpret_cast<Frames**>(block) = this;
        return block + header;
    }

    void Frames::Free(void* frame, std::size_t size)
    {
        auto* block = static_cast<uint8_t*>(frame) - header;
        auto& frames = **reinterpret_cast<Frames**>(block);
        frames.used -= Footprint(size);
        really_assert(frames.storage.begin() + frames.used == block);
    }

    std::size_t Frames::Peak() const
    {
        return peak;
    }

    void Claim::await_suspend(std::coroutine_handle<> awaiting)
    {
        handle = awaiting;
        claimer.Claim([this]
            {
                handle.resume();
            });
    }

    void DeviceRead::await_suspend(std::coroutine_handle<> awaiting)
    {
        handle = awaiting;
        device.ReadBlocks(buffer, block, [this](hal::BlockDevice::Result readResult)
            {
                result = readResult;
                handle.resume();
            });
    }
}
