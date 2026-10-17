#include "validation/firmware/cycles/CyclesCommands.hpp"

namespace validation
{
    CyclesCommands::CyclesCommands(services::HilContext& context, ResultLines& results, CycleRun& run, infra::MemoryRange<const CycleTarget> targets)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , results(results)
        , run(run)
        , targets(targets)
        , pending(context.response)
        , commands{ {
              services::HilBind<CyclesCommands, &CyclesCommands::Run>("cycles.run", "<subject> [n=<count>]", *this, context.response),
          } }
    {}

    infra::MemoryRange<const services::TerminalCommands::Command> CyclesCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    services::HilStatus CyclesCommands::Run(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "n" }))
            return services::HilStatus::usage;

        uint32_t runs = defaultRuns;
        auto status = services::HilStatus::done;
        arguments.Number("n", runs, 1, CycleRun::maximumRuns, status);
        if (status != services::HilStatus::done)
            return status;

        const auto* target = Find(arguments.Positional(0));
        if (target == nullptr)
            return services::HilStatus::unsupported;

        if (pending.Busy() || run.Running())
            return services::HilStatus::busy;

        auto operation = pending.Start(timeout);
        PrintDevice(results, target->device);
        run.Start(target->subject, target->device.name, runs, [this, operation](bool success)
            {
                Completed(operation, success);
            });
        return services::HilStatus::done;
    }

    const CycleTarget* CyclesCommands::Find(infra::BoundedConstString name) const
    {
        for (const auto& target : targets)
            if (target.subject.Name() == name)
                return &target;

        return nullptr;
    }

    void CyclesCommands::Completed(uint32_t operation, bool success)
    {
        if (!pending.Complete(operation))
            return;

        if (success)
            context.response.Ok();
        else
            context.response.Error(services::HilStatus::failed);
    }
}
