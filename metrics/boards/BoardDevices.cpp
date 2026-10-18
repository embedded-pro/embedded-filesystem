#include "metrics/boards/BoardDevices.hpp"
#include "metrics/fat/FatTargets.hpp"
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

        template<class Model>
        std::string DeviceJson(std::string_view name, const Model& model, double w6BoundMs)
        {
            auto json = Quoted(name) + ": { " + Quoted("model") + ": " + Quoted(model.name);
            for (const auto& [key, value] : Parameters(model))
                json += ", " + Quoted(key) + ": " + Quoted(value);

            return json + ", " + Quoted("w6BoundMs") + ": " + Quoted(Number(w6BoundMs)) + " }";
        }

        std::string DeviceJson(const NorBoardDevice& device)
        {
            return DeviceJson(device.name, *device.model, NorTargets{ *device.model, device.config, 1 }.CommitMilliseconds());
        }

        std::string DeviceJson(const BlockBoardDevice& device)
        {
            return DeviceJson(device.name, *device.model, FatTargets{ *device.model }.CommitMilliseconds());
        }

        template<class Device>
        void AppendDevices(std::string& json, std::string_view& separator, std::span<const Device> devices)
        {
            for (const auto& device : devices)
            {
                json += separator;
                json += "  " + DeviceJson(device);
                separator = ",\n";
            }
        }
    }

    const std::array<NorBoardDevice, 2> norBoardDevices{ {
        { "stm32h745i-disco/qspi", &norQspi16m, norQspi16mConfig },
        { "stm32h757i-eval/qspi", &norQspi16m, norQspi16mConfig },
    } };

    const std::array<BlockBoardDevice, 1> blockBoardDevices{ {
        { "stm32h757i-eval/sd", &sdCard256m },
    } };

    std::string DeviceModelsJson(std::span<const NorBoardDevice> norDevices, std::span<const BlockBoardDevice> blockDevices)
    {
        std::string json{ "{" };
        std::string_view separator{ "\n" };
        AppendDevices(json, separator, norDevices);
        AppendDevices(json, separator, blockDevices);
        return json + "\n}\n";
    }
}
