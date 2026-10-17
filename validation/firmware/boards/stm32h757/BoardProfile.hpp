#ifndef VALIDATION_FIRMWARE_BOARDS_STM32H757_BOARD_PROFILE_HPP
#define VALIDATION_FIRMWARE_BOARDS_STM32H757_BOARD_PROFILE_HPP

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "validation/firmware/boards/Memories.hpp"
#include <array>
#include <cstdint>
#include <utility>

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

    constexpr uint32_t sdRamBase = 0xd0000000;
    constexpr uint32_t sdRamSize = 32 * 1024 * 1024;

    constexpr std::array<MemoryRegion, 2> memoryRegions{ {
        { 0x60000000, 16 * 1024 * 1024, MemoryType::device },
        { sdRamBase, sdRamSize, MemoryType::normalCacheable },
    } };

    // The pins of the FMC on the MB1246, as the demo of hal-st sets them up; A20 to A22 (PE4 to PE6) of the parallel NOR are not listed yet
    inline constexpr std::array<std::pair<hal::Port, uint8_t>, 66> fmcPins = { { { hal::Port::F, 0 }, { hal::Port::F, 1 }, { hal::Port::F, 2 }, { hal::Port::F, 3 },
        { hal::Port::F, 4 }, { hal::Port::F, 5 }, { hal::Port::F, 12 }, { hal::Port::F, 13 },
        { hal::Port::F, 14 }, { hal::Port::F, 15 }, { hal::Port::G, 0 }, { hal::Port::G, 1 },
        { hal::Port::G, 2 }, { hal::Port::G, 3 }, { hal::Port::G, 4 }, { hal::Port::G, 5 },
        { hal::Port::D, 11 }, { hal::Port::D, 12 }, { hal::Port::D, 13 }, { hal::Port::E, 3 },
        { hal::Port::D, 14 }, { hal::Port::D, 15 }, { hal::Port::D, 0 }, { hal::Port::D, 1 },
        { hal::Port::E, 7 }, { hal::Port::E, 8 }, { hal::Port::E, 9 }, { hal::Port::E, 10 },
        { hal::Port::E, 11 }, { hal::Port::E, 12 }, { hal::Port::E, 13 }, { hal::Port::E, 14 },
        { hal::Port::E, 15 }, { hal::Port::D, 8 }, { hal::Port::D, 9 }, { hal::Port::D, 10 },
        { hal::Port::H, 8 }, { hal::Port::H, 9 }, { hal::Port::H, 10 }, { hal::Port::H, 11 },
        { hal::Port::H, 12 }, { hal::Port::H, 13 }, { hal::Port::H, 14 }, { hal::Port::H, 15 },
        { hal::Port::I, 0 }, { hal::Port::I, 1 }, { hal::Port::I, 2 }, { hal::Port::I, 3 },
        { hal::Port::I, 6 }, { hal::Port::I, 7 }, { hal::Port::I, 9 }, { hal::Port::I, 10 },
        { hal::Port::E, 0 }, { hal::Port::E, 1 }, { hal::Port::I, 4 }, { hal::Port::I, 5 },
        { hal::Port::D, 4 }, { hal::Port::D, 5 }, { hal::Port::D, 7 }, { hal::Port::G, 10 },
        { hal::Port::G, 8 }, { hal::Port::H, 5 }, { hal::Port::H, 6 }, { hal::Port::H, 7 },
        { hal::Port::F, 11 }, { hal::Port::G, 15 } } };

    constexpr hal::SdRamStm::Config SdRamConfig()
    {
        hal::SdRamStm::Config config{ sdRamBase, sdRamSize, 0x0603, 2, 9, 12, 32, 3 };
        config.timing.selfRefreshTime = 5;
        return config;
    }
}

#endif
