#if defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M'
#define FS_FIBER_CORTEX_M
#elif !defined(_WIN32)
#if defined(__APPLE__)
#define _XOPEN_SOURCE 700
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <new>
#include <ucontext.h>
#endif

#include "fs/common/Fiber.hpp"
#include <algorithm>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(FS_FIBER_CORTEX_M)
#if !defined(__ARM_ARCH_7M__) && !defined(__ARM_ARCH_7EM__) && !defined(__ARM_ARCH_8M_MAIN__)
#error "fs::Fiber switches stacks on Armv7-M, Armv7E-M and Armv8-M Mainline"
#endif

// Armv8-M faults when the stack pointer drops below the limit register of its stack, so each stack carries its own limit,
// and the limit is cleared while the stack pointer moves from one stack to the other
extern "C" [[gnu::naked, gnu::noinline]] void FsFiberSwitch(void**, void*)
{
    asm volatile(
        "push {r4-r11, lr}\n"
#if defined(__ARM_FP)
        "vpush {s16-s31}\n"
#endif
#if defined(__ARM_ARCH_8M_MAIN__)
        "mrs r3, control\n"
        "lsls r3, r3, #30\n"
        "ite mi\n"
        "mrsmi r2, psplim\n"
        "mrspl r2, msplim\n"
        "push {r2, r3}\n"
        "mov.w r3, #0\n"
        "ite mi\n"
        "msrmi psplim, r3\n"
        "msrpl msplim, r3\n"
#endif
        "mov r2, sp\n"
        "str r2, [r0]\n"
        "mov sp, r1\n"
#if defined(__ARM_ARCH_8M_MAIN__)
        "pop {r2, r3}\n"
        "ite mi\n"
        "msrmi psplim, r2\n"
        "msrpl msplim, r2\n"
#endif
#if defined(__ARM_FP)
        "vpop {s16-s31}\n"
#endif
        "pop {r4-r11, pc}\n");
}

extern "C" [[gnu::naked, gnu::noinline]] void FsFiberStart()
{
    asm volatile(
        "mov r0, r4\n"
        "blx r5\n"
        "b .\n");
}
#elif !defined(_WIN32)
namespace
{
    struct Contexts
    {
        ucontext_t fiber;
        ucontext_t caller;
    };

    ucontext_t& Context(void* context)
    {
        return *static_cast<ucontext_t*>(context);
    }
}
#endif

namespace fs
{
    namespace
    {
        constexpr uint8_t untouched = 0xa5;
    }

    class FiberEntry
    {
    public:
#if defined(FS_FIBER_CORTEX_M)
        static void Enter(Fiber* instance)
        {
            instance->RunBody();
        }
#elif defined(_WIN32)
        static void __stdcall Enter(void* instance)
        {
            for (;;)
                static_cast<Fiber*>(instance)->RunBody();
        }
#else
        static void Enter(unsigned int high, unsigned int low)
        {
            for (;;)
                reinterpret_cast<Fiber*>(static_cast<uintptr_t>((static_cast<uint64_t>(high) << 32) | low))->RunBody();
        }
#endif
    };

    Fiber::Fiber(infra::ByteRange stack)
        : stack(stack)
    {
#if !defined(FS_FIBER_CORTEX_M) && !defined(_WIN32)
        auto address = (reinterpret_cast<uintptr_t>(stack.end()) - sizeof(Contexts)) & ~uintptr_t{ alignof(Contexts) - 1 };
        auto* contexts = reinterpret_cast<Contexts*>(address);
        new (contexts) Contexts{};
        fiber = &contexts->fiber;
        caller = &contexts->caller;
        this->stack = infra::ByteRange(stack.begin(), reinterpret_cast<uint8_t*>(address));

        getcontext(&contexts->fiber);
        contexts->fiber.uc_stack.ss_sp = this->stack.begin();
        contexts->fiber.uc_stack.ss_size = this->stack.size();
        contexts->fiber.uc_link = nullptr;
        auto instance = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
        makecontext(&contexts->fiber, reinterpret_cast<void (*)()>(&FiberEntry::Enter), 2, static_cast<unsigned int>(instance >> 32), static_cast<unsigned int>(instance));
#endif
        std::fill(this->stack.begin(), this->stack.end(), untouched);
    }

    Fiber::~Fiber()
    {
#if defined(_WIN32)
        if (fiber != nullptr)
            DeleteFiber(fiber);
#endif
    }

    void Fiber::Run(infra::ClaimableResource::Claimer& runClaimer, const infra::Function<void()>& runBody)
    {
        claimer = &runClaimer;
        body = runBody;
        finished = false;

#if defined(FS_FIBER_CORTEX_M)
#if defined(__ARM_FP)
        constexpr std::size_t floatWords = 16;
#else
        constexpr std::size_t floatWords = 0;
#endif
#if defined(__ARM_ARCH_8M_MAIN__)
        constexpr std::size_t limitWords = 2;
#else
        constexpr std::size_t limitWords = 0;
#endif
        constexpr std::size_t words = limitWords + floatWords + 9;
        auto* frame = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(stack.end()) & ~uintptr_t{ 7 }) - words;
#if defined(__ARM_ARCH_8M_MAIN__)
        frame[0] = static_cast<uint32_t>((reinterpret_cast<uintptr_t>(stack.begin()) + 7) & ~uintptr_t{ 7 });
#endif
        frame[limitWords + floatWords] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
        frame[limitWords + floatWords + 1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FiberEntry::Enter));
        frame[words - 1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FsFiberStart));
        fiber = frame;
#elif defined(_WIN32)
        if (fiber == nullptr)
            fiber = CreateFiber(stack.size(), &FiberEntry::Enter, this);
#endif

        SwitchIn();
    }

    void Fiber::Suspend()
    {
#if defined(FS_FIBER_CORTEX_M)
        FsFiberSwitch(&fiber, caller);
#elif defined(_WIN32)
        SwitchToFiber(caller);
#else
        swapcontext(&Context(fiber), &Context(caller));
#endif
    }

    void Fiber::Resume()
    {
        SwitchIn();
    }

    std::size_t Fiber::Peak() const
    {
        auto used = std::find_if(stack.begin(), stack.end(), [](uint8_t byte)
            {
                return byte != untouched;
            });

        return static_cast<std::size_t>(stack.end() - used);
    }

    void Fiber::SwitchIn()
    {
#if defined(FS_FIBER_CORTEX_M)
        FsFiberSwitch(&caller, fiber);
#elif defined(_WIN32)
        caller = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
        SwitchToFiber(fiber);
#else
        swapcontext(&Context(caller), &Context(fiber));
#endif

        if (finished)
            claimer->Release();
    }

    void Fiber::RunBody()
    {
        body();
        finished = true;
        Suspend();
    }
}
