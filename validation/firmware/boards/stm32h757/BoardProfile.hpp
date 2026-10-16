#ifndef VALIDATION_FIRMWARE_BOARDS_STM32H757_BOARD_PROFILE_HPP
#define VALIDATION_FIRMWARE_BOARDS_STM32H757_BOARD_PROFILE_HPP

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "validation/firmware/boards/Memories.hpp"
#include <array>
#include <cstdint>

namespace validation::board
{
    constexpr const char* name = "stm32h757i-eval";
    constexpr const char* family = "stm32h7";
    constexpr uint32_t hseValue = 25'000'000;

    constexpr uint8_t terminalUart = 1;
    constexpr hal::Port terminalPort = hal::Port::B;
    constexpr uint8_t terminalTx = 14;
    constexpr uint8_t terminalRx = 15;
    constexpr uint32_t terminalBaudRate = 115'200;

    constexpr std::array<MemoryRegion, 2> memoryRegions{ {
        { 0x60000000, 16 * 1024 * 1024, MemoryType::device },
        { 0xd0000000, 32 * 1024 * 1024, MemoryType::normalCacheable },
    } };
}

#endif
