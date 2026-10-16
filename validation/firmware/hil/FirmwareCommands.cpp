#include "validation/firmware/hil/FirmwareCommands.hpp"

namespace validation
{
    FirmwareCommands::FirmwareCommands(services::HilContext& context, const FirmwareInfo& info)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , info(info)
        , commands{ {
              services::HilBind<FirmwareCommands, &FirmwareCommands::Info>("firmware.info", "", *this, context.response),
          } }
    {}

    infra::MemoryRange<const services::TerminalCommands::Command> FirmwareCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    services::HilStatus FirmwareCommands::Info(const services::HilArguments& arguments) const
    {
        if (!arguments.Shape(0, 0, {}))
            return services::HilStatus::usage;

        context.response.Ok() << " commit=" << info.commit << " cm4=" << (info.cortexM4Stopped ? "stopped" : "running");
        return services::HilStatus::done;
    }
}
