#include "validation/firmware/devices/RequestMeter.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "validation/firmware/timing/Nanoseconds.hpp"

namespace validation
{
    RequestMeter::RequestMeter(const CycleCounter& cycles, uint32_t clockHz)
        : cycles(cycles)
        , clockHz(clockHz)
    {
        really_assert(clockHz != 0);
    }

    void RequestMeter::OnTimeout(const infra::Function<void()>& onTimeout)
    {
        this->onTimeout = onTimeout;
    }

    bool RequestMeter::TimeoutWatched() const
    {
        return onTimeout != nullptr;
    }

    void RequestMeter::Start(uint64_t& totalNs, infra::Duration guard)
    {
        requestNs = &totalNs;
        guardTimer.Start(guard, [this]
            {
                // A copy, since the handler may replace itself
                auto handler = onTimeout;
                if (handler)
                    handler();
            });
        startCycles = cycles.Cycles();
    }

    void RequestMeter::Stop()
    {
        lastCycles = cycles.Cycles() - startCycles;
        guardTimer.Cancel();
        *requestNs += Nanoseconds(lastCycles, clockHz);
    }

    uint32_t RequestMeter::LastCycles() const
    {
        return lastCycles;
    }
}
