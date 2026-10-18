#include "fs/common/LiteLifecycle.hpp"

namespace fs
{
    bool LiteLifecycle::StartMount()
    {
        if (phase != Phase::unmounted && phase != Phase::fault)
            return false;

        phase = Phase::mounting;
        return true;
    }

    bool LiteLifecycle::StartUnmount()
    {
        if (phase != Phase::mounted)
            return false;

        phase = Phase::unmounting;
        return true;
    }

    void LiteLifecycle::Mounted(Result result)
    {
        phase = result == Result::success ? Phase::mounted : Phase::fault;
        fault = result;
    }

    void LiteLifecycle::Unmounted()
    {
        phase = Phase::unmounted;
    }

    bool LiteLifecycle::RemoveMedia()
    {
        if (phase != Phase::mounted)
            return false;

        phase = Phase::fault;
        fault = Result::mediaRemoved;
        return true;
    }

    bool LiteLifecycle::IsMounted() const
    {
        return phase == Phase::mounted;
    }

    bool LiteLifecycle::Mounting() const
    {
        return phase == Phase::mounting;
    }

    bool LiteLifecycle::Changing() const
    {
        return phase == Phase::mounting || phase == Phase::unmounting;
    }

    Result LiteLifecycle::Unavailable() const
    {
        return phase == Phase::fault && fault == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }
}
