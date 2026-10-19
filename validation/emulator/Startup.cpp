#include "hal/cortex_m/InterruptCortex.hpp"
#include "validation/emulator/Emulator.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

int main();

namespace
{
    constexpr uintptr_t coprocessorAccessControl = 0xe000ed88;
    constexpr uint32_t floatingPointAccess = 0xfu << 20;

    std::size_t Words(const uint32_t* begin, const uint32_t* end)
    {
        return (reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(begin)) / sizeof(uint32_t);
    }
}

extern "C"
{
    extern uint32_t _sidata[];
    extern uint32_t _sdata[];
    extern uint32_t _edata[];
    extern uint32_t _sbss[];
    extern uint32_t _ebss[];
    extern uint32_t _estack[];
    extern uint32_t _mainstacklimit[];

    void __libc_init_array();

    [[noreturn]] void Reset_Handler()
    {
        *reinterpret_cast<volatile uint32_t*>(coprocessorAccessControl) |= floatingPointAccess;
        asm volatile("dsb\nisb" ::: "memory");
#if defined(__ARM_ARCH_8M_MAIN__)
        asm volatile("msr msplim, %0" : : "r"(_mainstacklimit));
#endif
        std::copy_n(_sidata, Words(_sdata, _edata), _sdata);
        std::fill_n(_sbss, Words(_sbss, _ebss), 0u);
        __libc_init_array();
        main();
        validation::emulator::Finish();
    }

    void Default_Handler()
    {
        hal::cortex::InterruptTable::Instance().Invoke(hal::cortex::ActiveInterrupt());
    }

    // A fault can come from a stack that ran into its limit, so the handler continues on the top of the main stack
    [[gnu::naked]] void Fault_Handler()
    {
        asm volatile(
#if defined(__ARM_ARCH_8M_MAIN__)
            "movs r0, #0\n"
            "msr msplim, r0\n"
#endif
            "movw r0, #:lower16:_estack\n"
            "movt r0, #:upper16:_estack\n"
            "mov sp, r0\n"
            "b ReportFault\n");
    }

    using Handler = void (*)();

    [[gnu::section(".isr_vector"), gnu::used]] const std::array<Handler, 16> vectors{ {
        reinterpret_cast<Handler>(_estack),
        &Reset_Handler,
        &Fault_Handler,
        &Fault_Handler,
        &Fault_Handler,
        &Fault_Handler,
        &Fault_Handler,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &Default_Handler,
        &Default_Handler,
        nullptr,
        &Default_Handler,
        &Default_Handler,
    } };
}
