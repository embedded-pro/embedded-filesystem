#ifndef FS_FAT_FAT_CONFIG_HPP
#define FS_FAT_FAT_CONFIG_HPP

#include "infra/timer/Timer.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace fs
{
    struct FatLimits
    {
        std::size_t directories{ 1 };
        std::size_t pathOperations{ 2 };
        std::size_t extentsPerFile{ 4 };
        std::size_t closedFiles{ 2 };
        std::size_t freeRuns{ 8 };
        std::size_t maxNameBytes{ 255 };
    };

    struct FatFeatures
    {
        bool makeDirectory{ true };
        bool remove{ true };
        bool rename{ true };
        bool expectSize{ true };
        bool usage{ true };
        bool backgroundScan{ true };
        bool timestamps{ true };
        bool cleanShutdown{ true };
        bool longNames{ true };
    };

    struct FatConfig
    {
        std::size_t maxPathBytes{ 255 };
        infra::Duration idleDelay{ std::chrono::milliseconds(10) };
        bool discard{ true };
        uint32_t blocksPerAllocationUnit{ 0 };
        uint32_t timerServiceId{ infra::systemTimerServiceId };
    };
}

#endif
