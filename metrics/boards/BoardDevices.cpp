#include "metrics/boards/BoardDevices.hpp"
#include "metrics/nor/NorConfigurations.hpp"
#include "metrics/nor/NorTargets.hpp"
#include <charconv>

namespace metrics
{
    namespace
    {
        std::string Quoted(std::string_view text)
        {
            return "\"" + std::string{ text } + "\"";
        }

        std::string Number(double value)
        {
            std::array<char, 32> text{};
            auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), value);
            return std::string{ text.data(), end };
        }

        std::string DeviceJson(const NorBoardDevice& device)
        {
            auto json = Quoted(device.name) + ": { " + Quoted("model") + ": " + Quoted(device.model->name);
            for (const auto& [key, value] : Parameters(*device.model))
                json += ", " + Quoted(key) + ": " + Quoted(value);

            NorTargets targets{ *device.model, device.config, 1 };
            return json + ", " + Quoted("w6BoundMs") + ": " + Quoted(Number(targets.CommitMilliseconds())) + " }";
        }
    }

    const std::array<NorBoardDevice, 2> norBoardDevices{ {
        { "stm32h745i-disco/qspi", &norQspi16m, norQspi16mConfig },
        { "stm32h757i-eval/qspi", &norQspi16m, norQspi16mConfig },
    } };

    std::string DeviceModelsJson(std::span<const NorBoardDevice> devices)
    {
        std::string json{ "{" };
        std::string_view separator{ "\n" };
        for (const auto& device : devices)
        {
            json += separator;
            json += "  " + DeviceJson(device);
            separator = ",\n";
        }

        return json + "\n}\n";
    }
}
