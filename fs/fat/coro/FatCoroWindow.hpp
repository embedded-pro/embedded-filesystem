#ifndef FS_FAT_CORO_FAT_CORO_WINDOW_HPP
#define FS_FAT_CORO_FAT_CORO_WINDOW_HPP

#include "fs/fat/coro/Coroutine.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <limits>

namespace fs
{
    class FatCoroWindow
    {
    public:
        struct Load
        {
            bool await_ready() const noexcept
            {
                return window.loaded == sector;
            }

            void await_suspend(std::coroutine_handle<> awaiting)
            {
                window.Start(sector, awaiting);
            }

            Result await_resume() const noexcept
            {
                return window.loaded == sector ? Result::success : window.result;
            }

            FatCoroWindow& window;
            uint32_t sector;
        };

        FatCoroWindow(hal::BlockDevice& device, infra::ByteRange buffer);
        FatCoroWindow(const FatCoroWindow& other) = delete;
        FatCoroWindow& operator=(const FatCoroWindow& other) = delete;
        ~FatCoroWindow() = default;

        Load Read(uint32_t sector);
        infra::ConstByteRange Contents() const;
        uint32_t SectorSize() const;
        void Forget();

    private:
        void Start(uint32_t sector, std::coroutine_handle<> awaiting);

    private:
        static constexpr uint32_t none = std::numeric_limits<uint32_t>::max();

        hal::BlockDevice& device;
        infra::ByteRange buffer;
        uint32_t loaded{ none };
        uint32_t requested{ none };
        std::coroutine_handle<> handle;
        Result result{ Result::success };
    };
}

#endif
