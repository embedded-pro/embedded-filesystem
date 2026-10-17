#ifndef VALIDATION_FIRMWARE_WORKLOAD_IDLE_WAIT_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_IDLE_WAIT_HPP

#include "fs/interfaces/FileSystem.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/Function.hpp"
#include <chrono>

namespace validation
{
    // Waits for a time, or until the file system has no work left in the background
    class IdleWait
    {
    public:
        static constexpr infra::Duration pollInterval = std::chrono::milliseconds(1);

        explicit IdleWait(const infra::Function<void()>& onDone);

        void Wait(infra::Duration duration);
        void Settle(const fs::FileSystem& fileSystem, infra::Duration limit);
        void Cancel();

    private:
        void Poll();

    private:
        infra::Function<void()> onDone;
        const fs::FileSystem* fileSystem{ nullptr };
        infra::TimerSingleShot timer;
        infra::TimerRepeating pollTimer;
        infra::TimePoint deadline;
    };
}

#endif
