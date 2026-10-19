#include "fs/fat/fiber/Fiber.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include <algorithm>

#if defined(__arm__)
extern "C" __attribute__((naked, noinline)) void FsFiberSwitch(void**, void*)
{
    asm volatile(
        "push {r4-r11, lr}\n"
#if defined(__ARM_FP)
        "vpush {s16-s31}\n"
#endif
        "mov r2, sp\n"
        "str r2, [r0]\n"
        "mov sp, r1\n"
#if defined(__ARM_FP)
        "vpop {s16-s31}\n"
#endif
        "pop {r4-r11, pc}\n");
}

extern "C" __attribute__((naked, noinline)) void FsFiberStart()
{
    asm volatile(
        "mov r0, r4\n"
        "blx r5\n"
        "b .\n");
}
#endif

namespace fs::fiber
{
    namespace
    {
        constexpr uint8_t unused = 0xa5;
    }

    Stack::Stack(infra::ByteRange storage)
        : storage(storage)
    {
        std::fill(storage.begin(), storage.end(), unused);
    }

    bool Stack::Start(const infra::Function<void()>& newBody)
    {
        body = newBody;
        finished = false;

#if defined(__arm__)
#if defined(__ARM_FP)
        constexpr std::size_t floatWords = 16;
#else
        constexpr std::size_t floatWords = 0;
#endif
        constexpr std::size_t words = floatWords + 9;
        auto* top = reinterpret_cast<uint32_t*>(storage.begin() + (storage.size() & ~std::size_t{ 7 }));
        auto* frame = top - words;
        std::fill(frame, top, 0);
        frame[floatWords] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
        frame[floatWords + 1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Stack::Entry));
        frame[words - 1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FsFiberStart));
        fiber = frame;
#else
        getcontext(&fiber);
        fiber.uc_stack.ss_sp = storage.begin();
        fiber.uc_stack.ss_size = storage.size();
        fiber.uc_link = nullptr;
        auto address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
        makecontext(&fiber, reinterpret_cast<void (*)()>(reinterpret_cast<void*>(&Stack::Entry)), 2, static_cast<unsigned int>(address >> 32), static_cast<unsigned int>(address));
#endif

        return SwitchIn();
    }

    bool Stack::Resume()
    {
        return SwitchIn();
    }

    void Stack::Suspend()
    {
#if defined(__arm__)
        FsFiberSwitch(&fiber, caller);
#else
        swapcontext(&fiber, &caller);
#endif
    }

    std::size_t Stack::Peak() const
    {
        auto untouched = std::find_if(storage.begin(), storage.end(), [](uint8_t byte)
            {
                return byte != unused;
            });

        return static_cast<std::size_t>(storage.end() - untouched);
    }

#if defined(__arm__)
    void Stack::Entry(Stack* stack)
    {
        stack->body();
        stack->finished = true;
        stack->Suspend();
    }
#else
    void Stack::Entry(unsigned int high, unsigned int low)
    {
        auto* stack = reinterpret_cast<Stack*>(static_cast<uintptr_t>((static_cast<uint64_t>(high) << 32) | low));
        stack->body();
        stack->finished = true;
        stack->Suspend();
    }
#endif

    bool Stack::SwitchIn()
    {
#if defined(__arm__)
        FsFiberSwitch(&caller, fiber);
#else
        swapcontext(&caller, &fiber);
#endif
        return finished;
    }

    Runner::Runner(hal::BlockDevice& device, infra::ByteRange stack)
        : device(device)
        , stack(stack)
    {}

    void Runner::Run(infra::ClaimableResource::Claimer& claimer, const infra::Function<void()>& body)
    {
        running = &claimer;
        Settle(stack.Start(body));
    }

    Result Runner::Read(infra::ByteRange buffer, uint32_t block)
    {
        device.ReadBlocks(buffer, block, [this](hal::BlockDevice::Result readResult)
            {
                result = readResult;
                Settle(stack.Resume());
            });
        stack.Suspend();
        return ToResult(result);
    }

    std::size_t Runner::StackPeak() const
    {
        return stack.Peak();
    }

    void Runner::Settle(bool finished)
    {
        if (finished)
            running->Release();
    }
}
