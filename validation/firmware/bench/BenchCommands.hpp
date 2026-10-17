#ifndef VALIDATION_FIRMWARE_BENCH_BENCH_COMMANDS_HPP
#define VALIDATION_FIRMWARE_BENCH_BENCH_COMMANDS_HPP

#include "services/hil/HilArguments.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilStatus.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/bench/NorBench.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/hil/DeviceCommands.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>

namespace validation
{
    struct NorBenchTarget
    {
        const DeviceEntry& device;
        CountingFlash& flash;
    };

    class BenchCommands
        : public services::TerminalCommands
    {
    public:
        static constexpr infra::Duration timeout = std::chrono::minutes(30);

        BenchCommands(services::HilContext& context, NorBench& bench, infra::MemoryRange<const NorBenchTarget> targets);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Request
        {
            CountingFlash* flash;
            infra::BoundedConstString device;
            uint32_t size;
            uint32_t repetitions;
        };

        services::HilStatus Program(const services::HilArguments& arguments);
        services::HilStatus Erase(const services::HilArguments& arguments);
        services::HilStatus Read(const services::HilArguments& arguments);
        services::HilStatus Check(const services::HilArguments& arguments);

        services::HilStatus Parse(const services::HilArguments& arguments, bool sized, Request& request) const;
        const NorBenchTarget* Find(infra::BoundedConstString name) const;
        infra::Function<void(BenchOutcome outcome)> Start();
        void Completed(uint32_t operation, BenchOutcome outcome);

    private:
        services::HilContext& context;
        NorBench& bench;
        infra::MemoryRange<const NorBenchTarget> targets;
        services::HilPendingOperation pending;
        std::array<Command, 4> commands;
    };
}

#endif
