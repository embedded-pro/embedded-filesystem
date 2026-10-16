#ifndef VALIDATION_FOOTPRINT_PLATFORM_HPP
#define VALIDATION_FOOTPRINT_PLATFORM_HPP

#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/cortex_m/SystemTickTimerService.hpp"
#include "infra/event/EventDispatcherWithWeakPtr.hpp"
#include "validation/firmware/devices/NullBlockDevice.hpp"
#include "validation/firmware/devices/NullFlash.hpp"
#include "validation/footprint/Configurations.hpp"
#include <array>
#include <cstdint>

namespace validation::footprint
{
    constexpr uint32_t coreClockHz = 400000000;

    struct Platform
    {
        void Run();

        hal::cortex::InterruptTable::WithStorage<16> interruptTable;
        infra::EventDispatcherWithWeakPtr::WithSize<50> eventDispatcher;
        hal::cortex::SystemTickTimerService systemTick{ coreClockHz };
        NullFlash flash{ norSectors, norSectorSize };
        NullBlockDevice blockDevice{ blockDeviceBlocks };
        std::array<uint8_t, 64> buffer{};
    };
}

#endif
