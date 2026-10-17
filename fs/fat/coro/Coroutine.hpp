#ifndef FS_FAT_CORO_COROUTINE_HPP
#define FS_FAT_CORO_COROUTINE_HPP

#include "fs/common/BlockDeviceResult.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/ByteRange.hpp"
#include <array>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace fs::coro
{
    class Frames
    {
    public:
        explicit Frames(infra::ByteRange storage);
        Frames(const Frames& other) = delete;
        Frames& operator=(const Frames& other) = delete;
        ~Frames() = default;

        void* Allocate(std::size_t size);
        static void Free(void* frame, std::size_t size);
        std::size_t Peak() const;

    private:
        infra::ByteRange storage;
        std::size_t used{ 0 };
        std::size_t peak{ 0 };
    };

    template<std::size_t Bytes>
    struct FrameStorage
    {
        alignas(std::max_align_t) std::array<uint8_t, Bytes> bytes{};
    };

    struct PromiseBase
    {
        template<class... Args>
        static void* operator new(std::size_t size, Frames& frames, Args&...)
        {
            return frames.Allocate(size);
        }

        template<class Self, class... Args>
        static void* operator new(std::size_t size, Self&, Frames& frames, Args&...)
        {
            return frames.Allocate(size);
        }

        static void operator delete(void* frame, std::size_t size)
        {
            Frames::Free(frame, size);
        }

        void unhandled_exception() noexcept
        {
            std::abort();
        }
    };

    template<class T>
    class [[nodiscard]] Task
    {
    public:
        struct promise_type
            : PromiseBase
        {
            Task get_return_object() noexcept
            {
                return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            auto final_suspend() noexcept
            {
                struct Final
                {
                    bool await_ready() const noexcept
                    {
                        return false;
                    }

                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept
                    {
                        return handle.promise().continuation;
                    }

                    void await_resume() const noexcept
                    {}
                };

                return Final{};
            }

            void return_value(T result) noexcept
            {
                value = result;
            }

            std::coroutine_handle<> continuation;
            T value{};
        };

        Task(Task&& other) noexcept
            : handle(std::exchange(other.handle, nullptr))
        {}

        Task& operator=(Task&& other) = delete;

        ~Task()
        {
            if (handle)
                handle.destroy();
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept
        {
            handle.promise().continuation = awaiting;
            return handle;
        }

        T await_resume() const noexcept
        {
            return handle.promise().value;
        }

    private:
        explicit Task(std::coroutine_handle<promise_type> handle) noexcept
            : handle(handle)
        {}

    private:
        std::coroutine_handle<promise_type> handle;
    };

    struct Detached
    {
        struct promise_type
            : PromiseBase
        {
            Detached get_return_object() noexcept
            {
                return {};
            }

            std::suspend_never initial_suspend() noexcept
            {
                return {};
            }

            std::suspend_never final_suspend() noexcept
            {
                return {};
            }

            void return_void() noexcept
            {}
        };
    };

    struct Claim
    {
        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> awaiting);

        void await_resume() const noexcept
        {}

        infra::ClaimableResource::Claimer& claimer;
        std::coroutine_handle<> handle{};
    };

    struct DeviceRead
    {
        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> awaiting);

        Result await_resume() const noexcept
        {
            return ToResult(result);
        }

        hal::BlockDevice& device;
        infra::ByteRange buffer;
        uint32_t block;
        std::coroutine_handle<> handle{};
        hal::BlockDevice::Result result{ hal::BlockDevice::Result::success };
    };
}

#endif
