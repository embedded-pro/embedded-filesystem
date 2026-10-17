#include "validation/firmware/boards/EventInfrastructure.hpp"
#include "generated/stm32fxxx/PinoutTableDefault.hpp"
#include <chrono>

extern "C" uint32_t SystemCoreClock;

namespace validation
{
    DwtCycleCounter::DwtCycleCounter()
    {
        dwt.Start();
    }

    uint32_t DwtCycleCounter::Cycles() const
    {
        return dwt.Cycles();
    }

    EventInfrastructure::EventInfrastructure()
        : eventDispatcher(idle)
        , gpio(hal::pinoutTableDefaultStm, hal::analogTableDefaultStm)
        , systemTick(SystemCoreClock, std::chrono::milliseconds(1))
    {}

    void EventInfrastructure::Run()
    {
        eventDispatcher.Run();
    }
}
