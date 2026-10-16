#ifndef VALIDATION_FIRMWARE_BOARDS_EVENT_INFRASTRUCTURE_HPP
#define VALIDATION_FIRMWARE_BOARDS_EVENT_INFRASTRUCTURE_HPP

#include "hal/cortex_m/DataWatchpointAndTrace.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/cortex_m/LowPowerStrategyCortex.hpp"
#include "hal/cortex_m/SystemTickTimerService.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/event/LowPowerEventDispatcher.hpp"
#include "validation/firmware/cycles/CycleCounter.hpp"
#include "validation/firmware/cycles/IdleCycleCounter.hpp"

namespace validation
{
    class DwtCycleCounter
        : public CycleCounter
    {
    public:
        DwtCycleCounter();

        uint32_t Cycles() const override;

    private:
        hal::cortex::DataWatchpointAndTrace dwt;
    };

    // The low-power event infrastructure of hal-st, with a strategy that also counts the cycles the processor sleeps, for F-2
    struct EventInfrastructure
    {
        EventInfrastructure();

        void Run();

        hal::cortex::InterruptTable::WithStorage<main_::DefaultInterruptTableSize()> interruptTable;
        DwtCycleCounter cycles;
        hal::cortex::LowPowerStrategyCortex sleep;
        IdleCycleCounter idle{ sleep, cycles };
        infra::LowPowerEventDispatcher::WithSize<50> eventDispatcher;
        hal::GpioStm gpio;
        hal::cortex::SystemTickTimerService systemTick;
    };
}

#endif
