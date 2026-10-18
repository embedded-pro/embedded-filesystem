#include "validation/firmware/bench/BenchCommands.hpp"

namespace validation
{
    namespace
    {
        template<class Target>
        const Target* Find(infra::MemoryRange<const Target> targets, infra::BoundedConstString name)
        {
            for (const auto& target : targets)
                if (target.device.present && target.device.description.name == name)
                    return &target;

            return nullptr;
        }
    }

    BenchCommands::BenchCommands(services::HilContext& context, NorBench& bench, BlockBench& blockBench, infra::MemoryRange<const NorBenchTarget> targets, infra::MemoryRange<const BlockBenchTarget> blockTargets)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , bench(bench)
        , blockBench(blockBench)
        , targets(targets)
        , blockTargets(blockTargets)
        , pending(context.response)
        , commands{ {
              services::HilBind<BenchCommands, &BenchCommands::Program>("bench.program", "<device> size=<bytes> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Erase>("bench.erase", "<device> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Read>("bench.read", "<device> size=<bytes> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Check>("dev.check", "<device>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Blocks>("bench.blocks", "<device> <read|write> blocks=<count> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Units>("bench.units", "<device> units=<count> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Flush>("bench.flush", "<device> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Discard>("bench.discard", "<device> blocks=<count> n=<count>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const services::TerminalCommands::Command> BenchCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    services::HilStatus BenchCommands::Program(const services::HilArguments& arguments)
    {
        Request request{};
        auto status = Parse(arguments, true, request);
        if (status == services::HilStatus::done && !NorBench::ProgramFits(*request.flash, request.size, request.repetitions))
            status = services::HilStatus::range;
        if (status != services::HilStatus::done)
            return status;

        bench.Program(*request.flash, request.device, request.size, request.repetitions, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Erase(const services::HilArguments& arguments)
    {
        Request request{};
        auto status = Parse(arguments, false, request);
        if (status == services::HilStatus::done && !NorBench::EraseFits(*request.flash, request.repetitions))
            status = services::HilStatus::range;
        if (status != services::HilStatus::done)
            return status;

        bench.Erase(*request.flash, request.device, request.repetitions, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Read(const services::HilArguments& arguments)
    {
        Request request{};
        auto status = Parse(arguments, true, request);
        if (status == services::HilStatus::done && !NorBench::ReadFits(*request.flash, request.size, request.repetitions))
            status = services::HilStatus::range;
        if (status != services::HilStatus::done)
            return status;

        bench.Read(*request.flash, request.device, request.size, request.repetitions, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Check(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return services::HilStatus::usage;

        const auto* target = Find(targets, arguments.Positional(0));
        if (target == nullptr)
            return services::HilStatus::unsupported;

        if (Busy() || target->flash.InUse())
            return services::HilStatus::busy;

        bench.Check(target->flash, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Parse(const services::HilArguments& arguments, bool sized, Request& request) const
    {
        static constexpr std::array<const char*, 2> sizedKeys{ { "size", "n" } };
        static constexpr std::array<const char*, 1> countKeys{ { "n" } };
        auto keys = sized ? infra::MemoryRange<const char* const>(infra::MakeRange(sizedKeys)) : infra::MemoryRange<const char* const>(infra::MakeRange(countKeys));
        if (!arguments.Shape(1, 1, keys) || !arguments.Has("n") || (sized && !arguments.Has("size")))
            return services::HilStatus::usage;

        auto status = services::HilStatus::done;
        arguments.Number("n", request.repetitions, 1, NorBench::maximumRepetitions, status);
        arguments.Number("size", request.size, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status != services::HilStatus::done)
            return status;

        const auto* target = Find(targets, arguments.Positional(0));
        if (target == nullptr)
            return services::HilStatus::unsupported;

        if (Busy() || target->flash.InUse())
            return services::HilStatus::busy;

        request.flash = &target->flash;
        request.device = target->device.description.name;
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Blocks(const services::HilArguments& arguments)
    {
        BlockRequest request{};
        auto status = ParseBlock(arguments, 2, "blocks", request);
        auto operation = status == services::HilStatus::done ? arguments.Positional(1) : infra::BoundedConstString();
        if (status == services::HilStatus::done && operation != "read" && operation != "write")
            status = services::HilStatus::usage;
        if (status == services::HilStatus::done && !BlockBench::BlocksFit(*request.blocks, request.count, request.repetitions))
            status = services::HilStatus::range;
        if (status != services::HilStatus::done)
            return status;

        if (operation == "read")
            blockBench.Read(*request.blocks, request.device, request.count, request.repetitions, Start());
        else
            blockBench.Write(*request.blocks, request.device, request.count, request.repetitions, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Units(const services::HilArguments& arguments)
    {
        BlockRequest request{};
        auto status = ParseBlock(arguments, 1, "units", request);
        if (status == services::HilStatus::done && !BlockBench::UnitsFit(*request.blocks, request.count, request.repetitions))
            status = services::HilStatus::range;
        if (status != services::HilStatus::done)
            return status;

        blockBench.Units(*request.blocks, request.device, request.count, request.repetitions, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Flush(const services::HilArguments& arguments)
    {
        BlockRequest request{};
        auto status = ParseBlock(arguments, 1, nullptr, request);
        if (status == services::HilStatus::done && !BlockBench::FlushFits(*request.blocks, request.repetitions))
            status = services::HilStatus::range;
        if (status != services::HilStatus::done)
            return status;

        blockBench.Flush(*request.blocks, request.device, request.repetitions, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::Discard(const services::HilArguments& arguments)
    {
        BlockRequest request{};
        auto status = ParseBlock(arguments, 1, "blocks", request);
        if (status == services::HilStatus::done && !BlockBench::DiscardFits(*request.blocks, request.count, request.repetitions))
            status = services::HilStatus::range;
        if (status != services::HilStatus::done)
            return status;

        blockBench.Discard(*request.blocks, request.device, request.count, request.repetitions, Start());
        return services::HilStatus::done;
    }

    services::HilStatus BenchCommands::ParseBlock(const services::HilArguments& arguments, std::size_t positionals, const char* countKey, BlockRequest& request) const
    {
        const std::array<const char*, 2> keys{ { "n", countKey } };
        auto keyRange = infra::Head(infra::MemoryRange<const char* const>(infra::MakeRange(keys)), countKey != nullptr ? 2 : 1);
        if (!arguments.Shape(positionals, positionals, keyRange) || !arguments.Has("n") || (countKey != nullptr && !arguments.Has(countKey)))
            return services::HilStatus::usage;

        auto status = services::HilStatus::done;
        arguments.Number("n", request.repetitions, 1, Benchmark::maximumRepetitions, status);
        if (countKey != nullptr)
            arguments.Number(countKey, request.count, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status != services::HilStatus::done)
            return status;

        const auto* target = Find(blockTargets, arguments.Positional(0));
        if (target == nullptr)
            return services::HilStatus::unsupported;

        if (Busy() || target->blocks.InUse())
            return services::HilStatus::busy;

        request.blocks = &target->blocks;
        request.device = target->device.description.name;
        return services::HilStatus::done;
    }

    bool BenchCommands::Busy() const
    {
        return pending.Busy() || bench.Running() || blockBench.Running();
    }

    infra::Function<void(BenchOutcome outcome)> BenchCommands::Start()
    {
        auto operation = pending.Start(timeout);
        return [this, operation](BenchOutcome outcome)
        {
            Completed(operation, outcome);
        };
    }

    void BenchCommands::Completed(uint32_t operation, BenchOutcome outcome)
    {
        if (!pending.Complete(operation))
            return;

        if (outcome == BenchOutcome::done)
            context.response.Ok();
        else if (outcome == BenchOutcome::failed)
            context.response.Error(services::HilStatus::failed);
        else
            context.response.Error(services::HilStatus::timeout);
    }
}
