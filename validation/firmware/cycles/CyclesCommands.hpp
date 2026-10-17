#ifndef VALIDATION_FIRMWARE_CYCLES_CYCLES_COMMANDS_HPP
#define VALIDATION_FIRMWARE_CYCLES_CYCLES_COMMANDS_HPP

#include "services/hil/HilArguments.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilStatus.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/cycles/CycleRun.hpp"
#include "validation/firmware/cycles/CycleSubject.hpp"
#include "validation/firmware/results/DeviceRecord.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include <array>
#include <chrono>
#include <cstdint>

namespace validation
{
    struct CycleTarget
    {
        CycleSubject& subject;
        const DeviceDescription& device;
    };

    class CyclesCommands
        : public services::TerminalCommands
    {
    public:
        static constexpr infra::Duration timeout = std::chrono::minutes(10);
        static constexpr uint32_t defaultRuns = 100;

        CyclesCommands(services::HilContext& context, ResultLines& results, CycleRun& run, infra::MemoryRange<const CycleTarget> targets);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Run(const services::HilArguments& arguments);
        const CycleTarget* Find(infra::BoundedConstString name) const;
        void Completed(uint32_t operation, bool success);

    private:
        services::HilContext& context;
        ResultLines& results;
        CycleRun& run;
        infra::MemoryRange<const CycleTarget> targets;
        services::HilPendingOperation pending;
        std::array<Command, 1> commands;
    };
}

#endif
