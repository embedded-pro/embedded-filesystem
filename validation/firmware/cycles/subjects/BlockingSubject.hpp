#ifndef VALIDATION_FIRMWARE_CYCLES_SUBJECTS_BLOCKING_SUBJECT_HPP
#define VALIDATION_FIRMWARE_CYCLES_SUBJECTS_BLOCKING_SUBJECT_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/cycles/CycleSubject.hpp"
#include <array>

namespace validation
{
    // The baselines block in every call; their completions go through the event dispatcher, as those of the other subjects do
    class BlockingSubject
        : public CycleSubject
    {
    public:
        static constexpr int32_t shortTransfer = 100;
        static constexpr std::size_t maxPath = 32;

        bool Idle() const override;

    protected:
        BlockingSubject() = default;
        BlockingSubject(const BlockingSubject& other) = delete;
        BlockingSubject& operator=(const BlockingSubject& other) = delete;
        ~BlockingSubject() = default;

        void Complete(const Done& onDone, int32_t status);
        const char* Terminated(infra::BoundedConstString path);
        const char* TerminatedTarget(infra::BoundedConstString path);

    private:
        infra::AutoResetFunction<void(int32_t status)> pending;
        int32_t pendingStatus{ 0 };
        std::array<char, maxPath + 1> path{};
        std::array<char, maxPath + 1> target{};
    };
}

#endif
