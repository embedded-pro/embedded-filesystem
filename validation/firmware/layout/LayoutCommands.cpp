#include "validation/firmware/layout/LayoutCommands.hpp"

namespace validation
{
    namespace
    {
        const LayoutTarget* FindTarget(infra::MemoryRange<const LayoutTarget> targets, infra::BoundedConstString name)
        {
            for (const auto& target : targets)
                if (target.device.present && target.device.description.name == name)
                    return &target;

            return nullptr;
        }
    }

    LayoutCommands::LayoutCommands(services::HilContext& context, ResultLines& results, infra::MemoryRange<const LayoutTarget> targets)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , results(results)
        , targets(targets)
        , pending(context.response)
        , commands{ {
              services::HilBind<LayoutCommands, &LayoutCommands::Read>("layout.read", "<device>", *this, context.response),
              services::HilBind<LayoutCommands, &LayoutCommands::Format>("layout.format", "<device>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const services::TerminalCommands::Command> LayoutCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    services::HilStatus LayoutCommands::Read(const services::HilArguments& arguments)
    {
        auto status = Claim(arguments, readTimeout);
        if (status == services::HilStatus::done)
            target->card.ReadBlocks(infra::MakeRange(sector), 0, [this](hal::BlockDevice::Result result)
                {
                    FirstSectorRead(result);
                });
        return status;
    }

    // A quick format, since a discard of the whole data area of a large card outlasts what the driver waits for a request
    services::HilStatus LayoutCommands::Format(const services::HilArguments& arguments)
    {
        auto status = Claim(arguments, formatTimeout);
        if (status != services::HilStatus::done)
            return status;

        fs::FatFormatConfig config;
        config.discard = false;
        formatter.emplace(target->card);
        formatter->Format(config, [this](fs::Result result)
            {
                Formatted(result);
            });
        return status;
    }

    services::HilStatus LayoutCommands::Claim(const services::HilArguments& arguments, infra::Duration timeout)
    {
        if (!arguments.Shape(1, 1, {}))
            return services::HilStatus::usage;

        const auto* found = FindTarget(targets, arguments.Positional(0));
        if (found == nullptr)
            return services::HilStatus::unsupported;

        if (pending.Busy() || found->region.InUse())
            return services::HilStatus::busy;

        target = found;
        operation = pending.Start(timeout);
        target->region.OnTimeout([]
            {
                // The region takes no request while a layout command holds the card
            });
        return services::HilStatus::done;
    }

    void LayoutCommands::FirstSectorRead(hal::BlockDevice::Result result)
    {
        auto located = result == hal::BlockDevice::Result::success ? LocateVolume(infra::MakeRange(sector)) : std::nullopt;
        if (!located)
        {
            Fail();
            return;
        }

        place = *located;
        target->card.ReadBlocks(infra::MakeRange(sector), place.start, [this](hal::BlockDevice::Result result)
            {
                BootSectorRead(result);
            });
    }

    void LayoutCommands::BootSectorRead(hal::BlockDevice::Result result)
    {
        auto card = result == hal::BlockDevice::Result::success ? ParseVolume(infra::MakeRange(sector), place) : std::nullopt;
        auto formatted = fs::ComputeFormatLayout(target->card.NumberOfBlocks(), target->card.BlockSize(), fs::FatFormatConfig{});
        if (!card || !formatted)
            Fail();
        else
            Compare(*card, FormatterLayout(*formatted));
    }

    void LayoutCommands::Compare(const FatLayout& card, const FatLayout& formatter)
    {
        if (!Release())
            return;

        auto count = Differences(card, formatter, differences);
        PrintLayout("card", card, std::nullopt);
        PrintLayout("formatter", formatter, differences);
        context.response.Ok() << " differences=" << count;
    }

    void LayoutCommands::PrintLayout(const char* source, const FatLayout& layout, std::optional<infra::BoundedConstString> differs) const
    {
        auto record = results.Record("layout");
        record.Add("device", target->device.description.name)
            .Add("source", source)
            .Add("blocks", target->card.NumberOfBlocks())
            .Add("unitBlocks", target->unitBlocks);
        AddLayout(record, layout);
        if (differs)
            record.Add("differs", *differs);
    }

    void LayoutCommands::Formatted(fs::Result result)
    {
        if (!Release())
            return;

        if (result == fs::Result::success)
            context.response.Ok();
        else
            context.response.Error(services::HilStatus::failed);
    }

    void LayoutCommands::Fail()
    {
        if (Release())
            context.response.Error(services::HilStatus::failed);
    }

    // The card is released once its last request completes, also after the command timed out
    bool LayoutCommands::Release()
    {
        target->region.OnTimeout(nullptr);
        return pending.Complete(operation);
    }
}
