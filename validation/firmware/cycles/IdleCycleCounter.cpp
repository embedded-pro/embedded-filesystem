#include "validation/firmware/cycles/IdleCycleCounter.hpp"

namespace validation
{
    IdleCycleCounter::IdleCycleCounter(infra::LowPowerStrategy& strategy, const CycleCounter& counter)
        : strategy(strategy)
        , counter(counter)
    {}

    void IdleCycleCounter::RequestExecution()
    {
        strategy.RequestExecution();
    }

    void IdleCycleCounter::Idle(const infra::EventDispatcherWorker& eventDispatcher)
    {
        auto start = counter.Cycles();
        strategy.Idle(eventDispatcher);
        idleCycles += counter.Cycles() - start;
    }

    uint64_t IdleCycleCounter::IdleCycles() const
    {
        return idleCycles;
    }
}
