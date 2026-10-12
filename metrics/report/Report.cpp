#include "metrics/report/Report.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>

namespace metrics
{
    namespace
    {
        std::string Number(double value)
        {
            if (!std::isfinite(value))
                return "null";

            std::array<char, 32> text{};
            auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), value);
            return std::string{ text.data(), end };
        }

        std::string Quoted(std::string_view text)
        {
            static constexpr std::string_view hexDigits{ "0123456789abcdef" };

            std::string quoted{ "\"" };
            for (auto character : text)
            {
                auto byte = static_cast<unsigned char>(character);
                if (character == '"' || character == '\\')
                    quoted += { '\\', character };
                else if (byte < 0x20)
                    quoted += std::string{ "\\u00" } + hexDigits[byte >> 4] + hexDigits[byte & 0xf];
                else
                    quoted += character;
            }

            return quoted + "\"";
        }

        std::string Member(std::string_view name, std::string_view json)
        {
            return Quoted(name) + ": " + std::string{ json };
        }

        std::string BetterText(Better better)
        {
            return better == Better::higher ? "higher" : "lower";
        }

        std::string ResultJson(const MetricResult& result)
        {
            std::string json = "{ " + Member("metric", Quoted(result.metric)) + ", " + Member("workload", Quoted(result.workload)) + ", " +
                               Member("subject", Quoted(result.subject)) + ", " + Member("device", Quoted(result.device)) + ", " +
                               Member("unit", Quoted(result.unit));

            if (result.fill)
                json += ", " + Member("fill", std::to_string(*result.fill));

            json += ", " + Member("value", Number(result.value));

            if (result.target)
                json += ", " + Member("target", Quoted(result.target->Text()));

            if (result.pass)
                json += ", " + Member("pass", *result.pass ? "true" : "false");

            return json + ", " + Member("ramBytes", std::to_string(result.ramBytes)) + ", " + Member("better", Quoted(BetterText(result.better))) + " }";
        }

        std::string Indent(std::size_t depth)
        {
            return std::string(4 * depth, ' ');
        }

        template<class Items, class Format>
        std::string Joined(const Items& items, char open, char close, std::size_t depth, Format format)
        {
            if (items.empty())
                return { open, close };

            std::string json{ open };
            for (auto item = items.begin(); item != items.end(); ++item)
                json += (item == items.begin() ? "\n" : ",\n") + Indent(depth + 1) + format(*item);

            return json + "\n" + Indent(depth) + close;
        }
    }

    std::string Target::Text() const
    {
        std::string comparisonText{ comparison == Better::lower ? "<= " : ">= " };

        if (const auto* bound = std::get_if<double>(&reference))
            return comparisonText + Number(*bound);

        return comparisonText + std::get<std::string>(reference);
    }

    std::optional<bool> Target::Holds(double value) const
    {
        const auto* bound = std::get_if<double>(&reference);
        if (bound == nullptr)
            return std::nullopt;

        return comparison == Better::lower ? value <= *bound : value >= *bound;
    }

    Target AtMost(double bound)
    {
        return { Better::lower, bound };
    }

    Target AtLeast(double bound)
    {
        return { Better::higher, bound };
    }

    Target AtMostOf(std::string subjects)
    {
        return { Better::lower, std::move(subjects) };
    }

    Target AtLeastOf(std::string subjects)
    {
        return { Better::higher, std::move(subjects) };
    }

    const MetricResult& Report::Add(MetricResult result)
    {
        if (result.target)
            result.pass = result.target->Holds(result.value);

        results.push_back(std::move(result));
        return results.back();
    }

    void Report::AddDevice(const NorModel& model)
    {
        devices.emplace(model.name, metrics::Parameters(model));
    }

    void Report::AddDevice(const BlockDeviceModel& model)
    {
        devices.emplace(model.name, metrics::Parameters(model));
    }

    const std::vector<MetricResult>& Report::Results() const
    {
        return results;
    }

    std::string Report::Json() const
    {
        auto devicesJson = Joined(devices, '{', '}', 1, [](const auto& device)
            {
                return Member(device.first, Joined(device.second, '{', '}', 2, [](const auto& parameter)
                                                {
                                                    return Member(parameter.first, Quoted(parameter.second));
                                                }));
            });
        auto resultsJson = Joined(results, '[', ']', 1, ResultJson);

        return "{\n" + Indent(1) + Member("devices", devicesJson) + ",\n" + Indent(1) + Member("results", resultsJson) + "\n}\n";
    }

    bool Report::WriteTo(const std::string& path) const
    {
        std::ofstream file{ path, std::ios::binary | std::ios::trunc };
        file << Json();
        file.close();
        return !file.fail();
    }

    Report& GlobalReport()
    {
        static Report report;
        return report;
    }
}
