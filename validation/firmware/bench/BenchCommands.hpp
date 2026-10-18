#ifndef VALIDATION_FIRMWARE_BENCH_BENCH_COMMANDS_HPP
#define VALIDATION_FIRMWARE_BENCH_BENCH_COMMANDS_HPP

#include "services/hil/HilArguments.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilStatus.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/bench/BlockBench.hpp"
#include "validation/firmware/bench/NorBench.hpp"
#include "validation/firmware/devices/CountingBlockDevice.hpp"
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

    struct BlockBenchTarget
    {
        const DeviceEntry& device;
        CountingBlockDevice& blocks;
    };

    class BenchCommands
        : public services::TerminalCommands
    {
    public:
        static constexpr infra::Duration timeout = std::chrono::minutes(30);

        BenchCommands(services::HilContext& context, NorBench& bench, BlockBench& blockBench, infra::MemoryRange<const NorBenchTarget> targets, infra::MemoryRange<const BlockBenchTarget> blockTargets);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Request
        {
            CountingFlash* flash;
            infra::BoundedConstString device;
            uint32_t size;
            uint32_t repetitions;
        };

        struct BlockRequest
        {
            CountingBlockDevice* blocks;
            infra::BoundedConstString device;
            uint32_t count;
            uint32_t repetitions;
        };

        services::HilStatus Program(const services::HilArguments& arguments);
        services::HilStatus Erase(const services::HilArguments& arguments);
        services::HilStatus Read(const services::HilArguments& arguments);
        services::HilStatus Check(const services::HilArguments& arguments);
        services::HilStatus Blocks(const services::HilArguments& arguments);
        services::HilStatus Units(const services::HilArguments& arguments);
        services::HilStatus Flush(const services::HilArguments& arguments);
        services::HilStatus Discard(const services::HilArguments& arguments);

        services::HilStatus Parse(const services::HilArguments& arguments, bool sized, Request& request) const;
        services::HilStatus ParseBlock(const services::HilArguments& arguments, std::size_t positionals, const char* countKey, BlockRequest& request) const;
        bool Busy() const;
        infra::Function<void(BenchOutcome outcome)> Start();
        void Completed(uint32_t operation, BenchOutcome outcome);

    private:
        services::HilContext& context;
        NorBench& bench;
        BlockBench& blockBench;
        infra::MemoryRange<const NorBenchTarget> targets;
        infra::MemoryRange<const BlockBenchTarget> blockTargets;
        services::HilPendingOperation pending;
        std::array<Command, 8> commands;
    };
}

#endif
