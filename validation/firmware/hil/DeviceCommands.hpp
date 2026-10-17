#ifndef VALIDATION_FIRMWARE_HIL_DEVICE_COMMANDS_HPP
#define VALIDATION_FIRMWARE_HIL_DEVICE_COMMANDS_HPP

#include "infra/util/MemoryRange.hpp"
#include "services/hil/HilArguments.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilStatus.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/results/DeviceRecord.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include <array>

namespace validation
{
    struct DeviceEntry
    {
        DeviceDescription description;
        bool present;
    };

    const DeviceEntry* FindDevice(infra::MemoryRange<const DeviceEntry> devices, infra::BoundedConstString name);

    class DeviceCommands
        : public services::TerminalCommands
    {
    public:
        DeviceCommands(services::HilContext& context, ResultLines& results, infra::MemoryRange<const DeviceEntry> devices);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus List(const services::HilArguments& arguments) const;
        services::HilStatus Info(const services::HilArguments& arguments) const;

    private:
        services::HilContext& context;
        ResultLines& results;
        infra::MemoryRange<const DeviceEntry> devices;
        std::array<Command, 2> commands;
    };
}

#endif
