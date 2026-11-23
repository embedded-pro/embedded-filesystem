#pragma once

#include "infra/event/LowPowerEventDispatcher.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include <cstdint>

namespace validation
{
    class IdleCycleSource
    {
    protected:
        IdleCycleSource();
        IdleCycleSource(const IdleCycleSource& other) = delete;
        IdleCycleSource& operator=(const IdleCycleSource& other) = delete;
        ~IdleCycleSource() = default;

    public:
        virtual uint64_t IdleCycles() const = 0;
    };

    class IdleCycleCounter
        : public infra::LowPowerStrategy
        , public IdleCycleSource
    {
    public:
        IdleCycleCounter(infra::LowPowerStrategy& strategy, const CycleCounter& counter);

        void RequestExecution() override;
        void Idle(const infra::EventDispatcherWorker& eventDispatcher) override;

        uint64_t IdleCycles() const override;

    private:
        infra::LowPowerStrategy& strategy;
        const CycleCounter& counter;
        uint64_t idleCycles{ 0 };
    };
}
