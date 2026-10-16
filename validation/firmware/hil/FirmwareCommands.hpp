#ifndef VALIDATION_FIRMWARE_HIL_FIRMWARE_COMMANDS_HPP
#define VALIDATION_FIRMWARE_HIL_FIRMWARE_COMMANDS_HPP

#include "services/hil/HilArguments.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilStatus.hpp"
#include "services/util/Terminal.hpp"
#include <array>

namespace validation
{
    struct FirmwareInfo
    {
        const char* commit;
        bool cortexM4Stopped;
    };

    class FirmwareCommands
        : public services::TerminalCommands
    {
    public:
        FirmwareCommands(services::HilContext& context, const FirmwareInfo& info);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Info(const services::HilArguments& arguments);

    private:
        services::HilContext& context;
        FirmwareInfo info;
        std::array<Command, 1> commands;
    };
}

#endif
