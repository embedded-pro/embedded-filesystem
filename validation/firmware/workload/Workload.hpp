#ifndef VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_HPP

#include "infra/timer/Timer.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include "validation/firmware/workload/PhaseTotals.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace validation
{
    struct WorkloadParameters
    {
        uint64_t seed{ 0x5eedf11e };
        uint32_t scaleDivisor{ 16 };
        uint32_t fillPercent{ 0 };
    };

    constexpr uint32_t kibibyte = 1024;
    constexpr uint32_t mebibyte = 1024 * kibibyte;
    constexpr uint32_t chunkSize = 4 * kibibyte;
    constexpr infra::Duration settleLimit = std::chrono::minutes(10);

    using PhaseName = infra::BoundedString::WithStorage<PhaseTotals::maximumName>;
    using WorkloadPath = infra::BoundedString::WithStorage<24>;

    constexpr uint32_t Scaled(uint32_t size, const WorkloadParameters& parameters)
    {
        return std::max<uint32_t>(size / parameters.scaleDivisor, 1);
    }

    // Phases are named as on the host, with commas for spaces, since the values of result lines have no spaces
    PhaseName MakePhase(infra::BoundedConstString words, uint32_t value);
    PhaseName MakePhase(infra::BoundedConstString words, uint32_t first, uint32_t second);
    WorkloadPath MakePath(infra::BoundedConstString directory, uint32_t index);

    class Workload
    {
    protected:
        Workload();
        Workload(const Workload& other) = delete;
        Workload& operator=(const Workload& other) = delete;
        ~Workload() = default;

    public:
        virtual infra::BoundedConstString Name() const = 0;
        virtual bool Fills() const = 0;
        virtual void Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone) = 0;
    };
}

#endif
