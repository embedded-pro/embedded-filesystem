#include "validation/firmware/bench/BenchCommands.hpp"

namespace validation
{
    BenchCommands::BenchCommands(services::HilContext& context, NorBench& bench, infra::MemoryRange<const NorBenchTarget> targets)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , bench(bench)
        , targets(targets)
        , pending(context.response)
        , commands{ {
              services::HilBind<BenchCommands, &BenchCommands::Program>("bench.program", "<device> size=<bytes> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Erase>("bench.erase", "<device> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Read>("bench.read", "<device> size=<bytes> n=<count>", *this, context.response),
              services::HilBind<BenchCommands, &BenchCommands::Check>("dev.check", "<device>", *this, context.response),
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

        const auto* target = Find(arguments.Positional(0));
        if (target == nullptr)
            return services::HilStatus::unsupported;

        if (pending.Busy() || bench.Running())
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

        const auto* target = Find(arguments.Positional(0));
        if (target == nullptr)
            return services::HilStatus::unsupported;

        if (pending.Busy() || bench.Running())
            return services::HilStatus::busy;

        request.flash = &target->flash;
        request.device = target->device.description.name;
        return services::HilStatus::done;
    }

    const NorBenchTarget* BenchCommands::Find(infra::BoundedConstString name) const
    {
        for (const auto& target : targets)
            if (target.device.present && target.device.description.name == name)
                return &target;

        return nullptr;
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
