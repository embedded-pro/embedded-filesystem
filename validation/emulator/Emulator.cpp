#include "validation/emulator/Emulator.hpp"
#include "hal/cortex_m/Semihosting.hpp"
#include <array>

namespace validation::emulator
{
    namespace
    {
        constexpr uintptr_t applicationExit = 0x20026;
        constexpr uintptr_t runTimeError = 0x20023;
        constexpr uintptr_t configurableFaultStatus = 0xe000ed28;
        constexpr uint32_t stackOverflow = 1u << 20;

        bool failed = false;
        const char* expectedOverflow = nullptr;
    }

    void Print(const char* text)
    {
        hal::cortex::SemihostingWrite0(text);
    }

    void Print(const char* label, uint32_t value)
    {
        std::array<char, 12> digits{};
        auto index = digits.size() - 1;

        do
        {
            digits[--index] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);

        Print(label);
        Print(digits.data() + index);
        Print("\n");
    }

    void Check(bool condition, const char* what)
    {
        failed = failed || !condition;
        Print(condition ? "passed: " : "FAILED: ");
        Print(what);
        Print("\n");
    }

    void ExpectStackOverflow(const char* what)
    {
        expectedOverflow = what;
    }

    void Finish()
    {
        Print(failed ? "FAILED\n" : "PASSED\n");
        hal::cortex::SemihostingCall(hal::cortex::SemihostingOperation::exit, reinterpret_cast<void*>(failed ? runTimeError : applicationExit));

        while (true)
        {}
    }
}

extern "C" [[noreturn, gnu::used]] void ReportFault()
{
    auto status = *reinterpret_cast<volatile uint32_t*>(validation::emulator::configurableFaultStatus);

    if (validation::emulator::expectedOverflow != nullptr)
        validation::emulator::Check((status & validation::emulator::stackOverflow) != 0, validation::emulator::expectedOverflow);
    else
    {
        validation::emulator::Print("fault status ", status);
        validation::emulator::Check(false, "runs without a fault");
    }

    validation::emulator::Finish();
}
