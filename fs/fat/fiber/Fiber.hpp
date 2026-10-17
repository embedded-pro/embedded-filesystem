#ifndef FS_FAT_FIBER_FIBER_HPP
#define FS_FAT_FIBER_FIBER_HPP

#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

#if !defined(__arm__)
#include <ucontext.h>
#endif

namespace fs::fiber
{
    template<std::size_t Bytes>
    struct StackStorage
    {
        alignas(16) std::array<uint8_t, Bytes> bytes{};
    };

    class Stack
    {
    public:
        explicit Stack(infra::ByteRange storage);
        Stack(const Stack& other) = delete;
        Stack& operator=(const Stack& other) = delete;
        ~Stack() = default;

        bool Start(const infra::Function<void()>& body);
        bool Resume();
        void Suspend();
        std::size_t Peak() const;

    private:
#if defined(__arm__)
        static void Entry(Stack* stack);
#else
        static void Entry(unsigned int high, unsigned int low);
#endif
        bool SwitchIn();

    private:
        infra::ByteRange storage;
        infra::Function<void()> body;
        bool finished{ true };
#if defined(__arm__)
        void* fiber{ nullptr };
        void* caller{ nullptr };
#else
        ucontext_t fiber{};
        ucontext_t caller{};
#endif
    };

    class Runner
    {
    public:
        Runner(hal::BlockDevice& device, infra::ByteRange stack);
        Runner(const Runner& other) = delete;
        Runner& operator=(const Runner& other) = delete;
        ~Runner() = default;

        void Run(infra::ClaimableResource::Claimer& claimer, const infra::Function<void()>& body);
        Result Read(infra::ByteRange buffer, uint32_t block);
        std::size_t StackPeak() const;

    private:
        void Settle(bool finished);

    private:
        hal::BlockDevice& device;
        Stack stack;
        infra::ClaimableResource::Claimer* running{ nullptr };
        hal::BlockDevice::Result result{ hal::BlockDevice::Result::success };
    };
}

#endif
