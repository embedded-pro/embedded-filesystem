#ifndef VALIDATION_EMULATOR_EMULATOR_HPP
#define VALIDATION_EMULATOR_EMULATOR_HPP

#include <cstdint>

namespace validation::emulator
{
    void Print(const char* text);
    void Print(const char* label, uint32_t value);
    void Check(bool condition, const char* what);
    void ExpectStackOverflow(const char* what);
    [[noreturn]] void Finish();
}

#endif
