#include "hal/cortex_m/InterruptCortex.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

int main();

namespace
{
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

    void __libc_init_array();

    [[noreturn]] void Reset_Handler()
    {
        std::copy_n(_sidata, Words(_sdata, _edata), _sdata);
        std::fill_n(_sbss, Words(_sbss, _ebss), 0u);
        __libc_init_array();
        main();

        while (true)
        {}
    }

    void Default_Handler()
    {
        hal::cortex::InterruptTable::Instance().Invoke(hal::cortex::ActiveInterrupt());
    }

    using Handler = void (*)();

    [[gnu::section(".isr_vector"), gnu::used]] const std::array<Handler, 16> vectors{ {
        reinterpret_cast<Handler>(_estack),
        &Reset_Handler,
        &Default_Handler,
        &Default_Handler,
        &Default_Handler,
        &Default_Handler,
        &Default_Handler,
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
