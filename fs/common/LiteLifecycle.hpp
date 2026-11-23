#pragma once

#include "fs/interfaces/Result.hpp"
#include <cstdint>

namespace fs
{
    class LiteLifecycle
    {
    public:
        bool StartMount();
        bool StartUnmount();
        void Mounted(Result result);
        void Unmounted();
        bool RemoveMedia();

        bool IsMounted() const;
        bool Mounting() const;
        bool Changing() const;
        Result Unavailable() const;

    private:
        enum class Phase : uint8_t
        {
            unmounted,
            mounting,
            mounted,
            unmounting,
            fault
        };

        Phase phase{ Phase::unmounted };
        Result fault{ Result::notMounted };
    };
}
