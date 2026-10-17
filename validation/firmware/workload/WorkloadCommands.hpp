#ifndef VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_COMMANDS_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_COMMANDS_HPP

#include "services/hil/HilArguments.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilStatus.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/hil/DeviceCommands.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include "validation/firmware/workload/WorkloadRunner.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"
#include <array>
#include <chrono>
#include <cstdint>

namespace validation
{
    struct WorkloadTarget
    {
        const DeviceEntry& device;
        CountingFlash& flash;
        WorkloadVolume& volume;
    };

    class WorkloadCommands
        : public services::TerminalCommands
    {
    public:
        static constexpr infra::Duration formatTimeout = std::chrono::minutes(30);
        static constexpr infra::Duration runTimeout = std::chrono::hours(2);
        static constexpr uint32_t defaultScale = 16;
        static constexpr uint32_t maximumScale = 1024;
        static constexpr uint32_t maximumFill = 99;

        WorkloadCommands(services::HilContext& context, WorkloadRunner& runner, infra::MemoryRange<Workload* const> workloads, infra::MemoryRange<const WorkloadTarget> targets);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Request
        {
            const WorkloadTarget* target{ nullptr };
            Workload* workload{ nullptr };
            WorkloadParameters parameters;
        };

        services::HilStatus Format(const services::HilArguments& arguments);
        services::HilStatus Run(const services::HilArguments& arguments);
        services::HilStatus ParseRun(const services::HilArguments& arguments, Request& request) const;
        const WorkloadTarget* FindTarget(infra::BoundedConstString name) const;
        Workload* FindWorkload(infra::BoundedConstString name) const;
        void Formatted(fs::Result result);
        void Begin();
        void Finished();
        void Interrupted();

    private:
        services::HilContext& context;
        WorkloadRunner& runner;
        infra::MemoryRange<Workload* const> workloads;
        infra::MemoryRange<const WorkloadTarget> targets;
        services::HilPendingOperation pending;
        const WorkloadTarget* target{ nullptr };
        Workload* workload{ nullptr };
        WorkloadParameters parameters;
        uint32_t operation{ 0 };
        std::array<Command, 2> commands;
    };
}

#endif
