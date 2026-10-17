#include "validation/firmware/workload/WorkloadCommands.hpp"

namespace validation
{
    WorkloadCommands::WorkloadCommands(services::HilContext& context, WorkloadRunner& runner, infra::MemoryRange<Workload* const> workloads, infra::MemoryRange<const WorkloadTarget> targets)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , runner(runner)
        , workloads(workloads)
        , targets(targets)
        , pending(context.response)
        , commands{ {
              services::HilBind<WorkloadCommands, &WorkloadCommands::Format>("fs.format", "<device>", *this, context.response),
              services::HilBind<WorkloadCommands, &WorkloadCommands::Run>("wl.run", "<device> <workload> [fill=<percent>] [scale=<divisor>]", *this, context.response),
          } }
    {}

    infra::MemoryRange<const services::TerminalCommands::Command> WorkloadCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    services::HilStatus WorkloadCommands::Format(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return services::HilStatus::usage;

        const auto* found = FindTarget(arguments.Positional(0));
        if (found == nullptr)
            return services::HilStatus::unsupported;

        if (pending.Busy() || runner.Running())
            return services::HilStatus::busy;

        target = found;
        operation = pending.Start(formatTimeout);
        runner.Release([this]
            {
                target->volume.Format([this](fs::Result result)
                    {
                        Formatted(result);
                    });
            });
        return services::HilStatus::done;
    }

    services::HilStatus WorkloadCommands::Run(const services::HilArguments& arguments)
    {
        Request request;
        auto status = ParseRun(arguments, request);
        if (status != services::HilStatus::done)
            return status;

        target = request.target;
        workload = request.workload;
        parameters = request.parameters;
        operation = pending.Start(runTimeout);
        runner.Release([this]
            {
                Begin();
            });
        return services::HilStatus::done;
    }

    services::HilStatus WorkloadCommands::ParseRun(const services::HilArguments& arguments, Request& request) const
    {
        if (!arguments.Shape(2, 2, { "fill", "scale" }))
            return services::HilStatus::usage;

        auto status = services::HilStatus::done;
        request.parameters.scaleDivisor = defaultScale;
        arguments.Number("fill", request.parameters.fillPercent, 0, maximumFill, status);
        arguments.Number("scale", request.parameters.scaleDivisor, 1, maximumScale, status);
        if (status != services::HilStatus::done)
            return status;

        request.target = FindTarget(arguments.Positional(0));
        request.workload = FindWorkload(arguments.Positional(1));
        if (request.target == nullptr || request.workload == nullptr)
            return services::HilStatus::unsupported;

        if (request.parameters.fillPercent != 0 && !request.workload->Fills())
            return services::HilStatus::range;

        if (pending.Busy() || runner.Running())
            return services::HilStatus::busy;

        return services::HilStatus::done;
    }

    const WorkloadTarget* WorkloadCommands::FindTarget(infra::BoundedConstString name) const
    {
        for (const auto& candidate : targets)
            if (candidate.device.present && candidate.device.description.name == name)
                return &candidate;

        return nullptr;
    }

    Workload* WorkloadCommands::FindWorkload(infra::BoundedConstString name) const
    {
        for (auto* candidate : workloads)
            if (candidate->Name() == name)
                return candidate;

        return nullptr;
    }

    void WorkloadCommands::Formatted(fs::Result result)
    {
        if (!pending.Complete(operation))
            return;

        if (result == fs::Result::success)
            context.response.Ok();
        else
            context.response.Error(services::HilStatus::failed);
    }

    void WorkloadCommands::Begin()
    {
        runner.Begin(target->volume, target->flash, { target->device.description.name, target->volume.Subject(), workload->Name(), parameters.fillPercent, parameters.scaleDivisor, target->volume.RamBytes() }, [this]
            {
                Interrupted();
            });
        workload->Run(parameters, [this]
            {
                runner.End();
                runner.Release([this]
                    {
                        Finished();
                    });
            });
    }

    void WorkloadCommands::Finished()
    {
        if (!pending.Complete(operation))
            return;

        if (runner.Failed())
            context.response.Error(services::HilStatus::failed);
        else
            context.response.Ok();
    }

    void WorkloadCommands::Interrupted()
    {
        if (pending.Complete(operation))
            context.response.Error(services::HilStatus::timeout);
    }
}
