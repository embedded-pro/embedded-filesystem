#include "validation/firmware/hil/DeviceCommands.hpp"

namespace validation
{
    const DeviceEntry* FindDevice(infra::MemoryRange<const DeviceEntry> devices, infra::BoundedConstString name)
    {
        for (const auto& device : devices)
            if (device.present && device.description.name == name)
                return &device;

        return nullptr;
    }

    DeviceCommands::DeviceCommands(services::HilContext& context, ResultLines& results, infra::MemoryRange<const DeviceEntry> devices)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , results(results)
        , devices(devices)
        , commands{ {
              services::HilBind<DeviceCommands, &DeviceCommands::List>("dev.list", "", *this, context.response),
              services::HilBind<DeviceCommands, &DeviceCommands::Info>("dev.info", "<device>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const services::TerminalCommands::Command> DeviceCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    services::HilStatus DeviceCommands::List(const services::HilArguments& arguments) const
    {
        if (!arguments.Shape(0, 0, {}))
            return services::HilStatus::usage;

        auto line = context.response.Ok();
        line << " devices=";
        const char* separator = "";
        for (const auto& device : devices)
            if (device.present)
            {
                line << separator << device.description.name;
                separator = ",";
            }

        return services::HilStatus::done;
    }

    services::HilStatus DeviceCommands::Info(const services::HilArguments& arguments) const
    {
        if (!arguments.Shape(1, 1, {}))
            return services::HilStatus::usage;

        const auto* device = FindDevice(devices, arguments.Positional(0));
        if (device == nullptr)
            return services::HilStatus::unsupported;

        PrintDevice(results, device->description);
        context.response.Ok();
        return services::HilStatus::done;
    }
}
