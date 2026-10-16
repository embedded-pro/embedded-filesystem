#ifndef VALIDATION_FIRMWARE_CYCLES_SUBJECTS_BLOCKING_SUBJECT_HPP
#define VALIDATION_FIRMWARE_CYCLES_SUBJECTS_BLOCKING_SUBJECT_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/cycles/CycleSubject.hpp"
#include <array>

namespace validation
{
    // The calls of the baselines block, yet complete through the event dispatcher, so that every subject pays for one dispatch per call
    class BlockingSubject
        : public CycleSubject
    {
    public:
        static constexpr std::size_t maxPath = 32;

        bool Idle() const override;

    protected:
        BlockingSubject() = default;
        BlockingSubject(const BlockingSubject& other) = delete;
        BlockingSubject& operator=(const BlockingSubject& other) = delete;
        ~BlockingSubject();

        void Complete(const Done& onDone, int32_t status);
        const char* Terminated(infra::BoundedConstString text);
        const char* TerminatedTarget(infra::BoundedConstString text);

    private:
        infra::AutoResetFunction<void(int32_t status)> pending;
        int32_t pendingStatus{ 0 };
        std::array<char, maxPath + 1> path{};
        std::array<char, maxPath + 1> target{};
    };
}

#endif
