#ifndef VALIDATION_FIRMWARE_LAYOUT_LAYOUT_COMMANDS_HPP
#define VALIDATION_FIRMWARE_LAYOUT_LAYOUT_COMMANDS_HPP

#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "services/hil/HilArguments.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilStatus.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/devices/CountedDevice.hpp"
#include "validation/firmware/hil/DeviceCommands.hpp"
#include "validation/firmware/layout/FatLayout.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace validation
{
    struct LayoutTarget
    {
        const DeviceEntry& device;
        hal::BlockDevice& card;
        uint32_t unitBlocks;
        CountedDevice& region;
    };

    // The layout of a whole card against that of fs::FatFormatter, and a format of the whole card; the counting device of
    // the region of the card is claimed meanwhile, so that no benchmark or workload runs on the card
    class LayoutCommands
        : public services::TerminalCommands
    {
    public:
        static constexpr infra::Duration readTimeout = std::chrono::seconds(10);
        static constexpr infra::Duration formatTimeout = std::chrono::minutes(30);

        LayoutCommands(services::HilContext& context, ResultLines& results, infra::MemoryRange<const LayoutTarget> targets);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Read(const services::HilArguments& arguments);
        services::HilStatus Format(const services::HilArguments& arguments);
        services::HilStatus Claim(const services::HilArguments& arguments, infra::Duration timeout);
        void FirstSectorRead(hal::BlockDevice::Result result);
        void BootSectorRead(hal::BlockDevice::Result result);
        void Compare(const FatLayout& card, const FatLayout& formatter);
        void PrintLayout(const char* source, const FatLayout& layout, std::optional<infra::BoundedConstString> differs) const;
        void Formatted(fs::Result result);
        void Fail();
        bool Release();

    private:
        services::HilContext& context;
        ResultLines& results;
        infra::MemoryRange<const LayoutTarget> targets;
        services::HilPendingOperation pending;
        const LayoutTarget* target{ nullptr };
        uint32_t operation{ 0 };
        VolumePlace place{};
        alignas(fs::bufferAlignment) std::array<uint8_t, 512> sector{};
        std::optional<fs::FatFormatter::WithStorage<8>> formatter;
        infra::BoundedString::WithStorage<128> differences;
        std::array<Command, 2> commands;
    };
}

#endif
