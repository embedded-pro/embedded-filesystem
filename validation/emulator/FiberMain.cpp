#include "fs/common/Fiber.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/cortex_m/SystemTick.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/emulator/Emulator.hpp"
#include <array>
#include <chrono>
#include <cstdint>

extern "C" [[gnu::naked, gnu::noinline]] void SwitchWithMarks(const uint32_t*, void (*)(), uint32_t*)
{
    asm volatile(
        "push {r4-r11, lr}\n"
        "vpush {s16-s31}\n"
        "push {r2, r3}\n"
        "ldm r0!, {r4-r11}\n"
        "vldm r0, {s16-s31}\n"
        "blx r1\n"
        "pop {r2, r3}\n"
        "stm r2!, {r4-r11}\n"
        "vstm r2, {s16-s31}\n"
        "vpop {s16-s31}\n"
        "pop {r4-r11, pc}\n");
}

#if defined(__ARM_ARCH_8M_MAIN__)
extern "C" [[gnu::naked, gnu::noinline]] void RunOnProcessStack(void (*)(), uint32_t*, uint32_t*)
{
    asm volatile(
        "push {r4, lr}\n"
        "msr psp, r1\n"
        "msr psplim, r2\n"
        "mrs r4, control\n"
        "orr r3, r4, #2\n"
        "msr control, r3\n"
        "isb\n"
        "blx r0\n"
        "msr control, r4\n"
        "isb\n"
        "pop {r4, pc}\n");
}
#endif

namespace
{
    using validation::emulator::Check;
    using validation::emulator::Print;
    using Registers = std::array<uint32_t, 24>;

    hal::cortex::InterruptTable::WithStorage<16> interruptTable;
    infra::EventDispatcher::WithSize<16> dispatcher;
    alignas(16) std::array<uint8_t, fs::Fiber::defaultStackBytes> stack;
    fs::Fiber fiber{ infra::MakeRange(stack) };
    infra::ClaimableResource resource;
    infra::ClaimableResource::Claimer claimer{ resource };
    infra::Function<void()> body;

    void Run(const infra::Function<void()>& newBody)
    {
        body = newBody;
        claimer.Claim([]
            {
                fiber.Run(claimer, body);
            });
        dispatcher.ExecuteAllActions();
    }

    void Suspend()
    {
        fiber.Suspend();
    }

    void Resume()
    {
        fiber.Resume();
    }

    uint32_t Address(const void* pointer)
    {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
    }

    uint32_t StackPointer()
    {
        uint32_t value;
        asm volatile("mov %0, sp" : "=r"(value));
        return value;
    }

    bool OnFiberStack(uint32_t address)
    {
        return address >= Address(stack.data()) && address < Address(stack.data() + stack.size());
    }

    bool SwitchKeeps(uint32_t mark, void (*action)())
    {
        Registers marks{};
        Registers held{};

        for (uint32_t i = 0; i != marks.size(); ++i)
            marks[i] = mark + i;

        SwitchWithMarks(marks.data(), action, held.data());
        return held == marks;
    }

    int suspensions = 0;
    bool fiberKept = true;

    void RegistersSurviveTheSwitch()
    {
        Run([]
            {
                for (uint32_t i = 0; i != 3; ++i)
                {
                    ++suspensions;
                    fiberKept = SwitchKeeps(0x1000 + 0x100 * i, &Suspend) && fiberKept;
                }
            });

        auto mainKept = true;

        for (uint32_t i = 0; i != 3; ++i)
            mainKept = SwitchKeeps(0x2000 + 0x100 * i, &Resume) && mainKept;

        Check(suspensions == 3, "a body suspends and resumes three times");
        Check(fiberKept, "the fiber keeps r4-r11 and s16-s31 while the main stack runs");
        Check(mainKept, "the main stack keeps r4-r11 and s16-s31 while the fiber runs");
        Check(!resource.IsClaimed(), "the claim is released when the body ends");
    }

    class TickCounter
        : public hal::cortex::InterruptHandler
    {
    public:
        void Start()
        {
            Register(hal::cortex::sysTickIrq);
            tick.Enable();
        }

        void Stop()
        {
            tick.Disable();
            Unregister();
        }

        uint32_t Ticks() const
        {
            return ticks;
        }

        uint32_t TicksOnFiber() const
        {
            return ticksOnFiber;
        }

        void Invoke() override
        {
            ticks = ticks + 1;

            if (OnFiberStack(StackPointer()))
                ticksOnFiber = ticksOnFiber + 1;

            sum = sum * 1.0001f + 1.0f;
        }

    private:
        hal::cortex::SystemTick tick{ 25000000, std::chrono::microseconds(80) };
        volatile uint32_t ticks = 0;
        volatile uint32_t ticksOnFiber = 0;
        volatile float sum = 0.0f;
    };

    TickCounter tickCounter;

    [[gnu::noinline]] float Series(uint32_t iterations, bool suspend)
    {
        auto a = 1.0f;
        auto b = 0.5f;
        auto c = 0.25f;
        auto d = 0.125f;
        uint32_t x = 1;

        for (uint32_t i = 0; i != iterations; ++i)
        {
            a = a * 0.999f + b;
            b = b * 0.998f + c;
            c = c * 0.997f + d;
            d = d * 0.996f + 0.001f;
            x = x * 1664525u + 1013904223u;

            if (suspend && i % 5000 == 4999)
                fiber.Suspend();
        }

        return a + b + c + d + static_cast<float>(x & 0xffff);
    }

    constexpr uint32_t iterations = 200000;
    float onFiber = 0.0f;
    bool done = false;

    void InterruptsRunOnTheFiberStack()
    {
        tickCounter.Start();
        Run([]
            {
                onFiber = Series(iterations, true);
                done = true;
            });

        while (!done)
            fiber.Resume();

        tickCounter.Stop();
        auto onMain = Series(iterations, false);

        Print("ticks ", tickCounter.Ticks());
        Print("ticks on the fiber stack ", tickCounter.TicksOnFiber());
        Print("stack peak ", static_cast<uint32_t>(fiber.Peak()));
        Check(tickCounter.TicksOnFiber() != 0, "interrupts run on the fiber stack");
        Check(onFiber == onMain, "the fiber computes under interrupts what the main stack computes");
        Check(fiber.Peak() != 0 && fiber.Peak() < stack.size(), "the peak of the stack is within the stack");
    }

#if defined(__ARM_ARCH_8M_MAIN__)
    uint32_t MainLimit()
    {
        uint32_t value;
        asm volatile("mrs %0, msplim" : "=r"(value));
        return value;
    }

    uint32_t ProcessLimit()
    {
        uint32_t value;
        asm volatile("mrs %0, psplim" : "=r"(value));
        return value;
    }

    alignas(8) std::array<uint32_t, 1024> processStack;
    uint32_t mainLimitOnFiber = 0;
    uint32_t processLimitOnFiber = 0;
    uint32_t mainLimitAfterResume = 0;
    uint32_t processLimitAfterBody = 0;
    uint32_t mainLimitAfterBody = 0;

    void ReadLimitsOnFiber()
    {
        Run([]
            {
                mainLimitOnFiber = MainLimit();
                processLimitOnFiber = ProcessLimit();
                fiber.Suspend();
                mainLimitAfterResume = MainLimit();
            });
    }

    void ReadLimitsFromProcessStack()
    {
        ReadLimitsOnFiber();
        fiber.Resume();
        processLimitAfterBody = ProcessLimit();
        mainLimitAfterBody = MainLimit();
    }

    void EachStackKeepsItsLimit()
    {
        auto mainLimit = MainLimit();
        auto fiberLimit = Address(stack.data());

        ReadLimitsOnFiber();
        Check(MainLimit() == mainLimit, "MSPLIM is the limit of the main stack while the fiber is suspended");
        fiber.Resume();
        Check(mainLimitOnFiber == fiberLimit, "MSPLIM is the limit of the fiber on the fiber");
        Check(mainLimitAfterResume == fiberLimit, "MSPLIM is the limit of the fiber after a resume");
        Check(processLimitOnFiber == 0, "PSPLIM stays as it is when the main stack runs the fiber");
        Check(MainLimit() == mainLimit, "MSPLIM is the limit of the main stack after the body");

        RunOnProcessStack(&ReadLimitsFromProcessStack, processStack.data() + processStack.size(), processStack.data());
        Check(processLimitOnFiber == fiberLimit, "PSPLIM is the limit of the fiber when the process stack runs the fiber");
        Check(mainLimitOnFiber == mainLimit, "MSPLIM stays as it is when the process stack runs the fiber");
        Check(processLimitAfterBody == Address(processStack.data()), "PSPLIM is the limit of the process stack after the body");
        Check(mainLimitAfterBody == mainLimit, "MSPLIM is the limit of the main stack after the body on the process stack");
    }

    [[gnu::noinline]] void Overflow()
    {
        volatile uint8_t bytes[2 * fs::Fiber::defaultStackBytes];
        bytes[0] = 1;
        bytes[sizeof(bytes) - 1] = 2;
    }

    void AnOverflowFaults()
    {
        constexpr uintptr_t systemHandlerControl = 0xe000ed24;
        constexpr uint32_t usageFaultEnable = 1u << 18;

        *reinterpret_cast<volatile uint32_t*>(systemHandlerControl) |= usageFaultEnable;
        validation::emulator::ExpectStackOverflow("a stack overflow on the fiber faults on its limit");
        Run([]
            {
                Overflow();
            });
        Check(false, "a stack overflow on the fiber faults on its limit");
    }
#endif
}

int main()
{
    RegistersSurviveTheSwitch();
    InterruptsRunOnTheFiberStack();
#if defined(__ARM_ARCH_8M_MAIN__)
    EachStackKeepsItsLimit();
    AnOverflowFaults();
#endif
    return 0;
}
