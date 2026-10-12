#include "metrics/device/DeviceModels.hpp"
#include <array>
#include <chrono>

namespace metrics
{
    using namespace std::chrono_literals;

    namespace
    {
        constexpr NorTiming qspiTypical{ 400us, 45ms, 50'000'000 };
        constexpr NorTiming qspiMaximum{ 3ms, 400ms, 50'000'000 };
        constexpr NorTiming internalTiming{ 8us, 20ms, 100'000'000 };

        uint64_t Nanoseconds(infra::Duration duration)
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
        }

        void AddTiming(std::vector<std::pair<std::string, std::string>>& parameters, const std::string& prefix, const NorTiming& timing)
        {
            parameters.emplace_back(prefix + ".pageProgramNs", std::to_string(Nanoseconds(timing.pageProgram)));
            parameters.emplace_back(prefix + ".sectorEraseNs", std::to_string(Nanoseconds(timing.sectorErase)));
            parameters.emplace_back(prefix + ".readBytesPerSecond", std::to_string(timing.readBytesPerSecond));
        }

        void AddTiming(std::vector<std::pair<std::string, std::string>>& parameters, const std::string& prefix, const BlockDeviceTiming& timing)
        {
            parameters.emplace_back(prefix + ".commandNs", std::to_string(Nanoseconds(timing.command)));
            parameters.emplace_back(prefix + ".blockReadNs", std::to_string(Nanoseconds(timing.blockRead)));
            parameters.emplace_back(prefix + ".blockWrittenNs", std::to_string(Nanoseconds(timing.blockWritten)));
            parameters.emplace_back(prefix + ".writeBusyNs", std::to_string(Nanoseconds(timing.writeBusy)));
            parameters.emplace_back(prefix + ".allocationUnitOpeningNs", std::to_string(Nanoseconds(timing.allocationUnitOpening)));
        }
    }

    const NorModel norQspi16m{ "nor-qspi-16m", 4096, 4096, 256, 1, qspiTypical, qspiMaximum };
    const NorModel norQspi1m{ "nor-qspi-1m", 256, 4096, 256, 1, qspiTypical, qspiMaximum };
    const NorModel internalFlash{ "internal-flash", 256, 2048, 16, 16, internalTiming, internalTiming };
    const BlockDeviceModel sdCard256m{ "sd-card-256m", 512, 524288, 8192, 2, { 100us, 25us, 50us, 0us, 10ms }, { 100us, 25us, 50us, 250ms, 10ms } };

    const NorTiming& NorModel::Timing(TimingCase timingCase) const
    {
        return timingCase == TimingCase::typical ? typical : maximum;
    }

    const BlockDeviceTiming& BlockDeviceModel::Timing(TimingCase timingCase) const
    {
        return timingCase == TimingCase::typical ? typical : maximum;
    }

    const NorModel* FindNorModel(std::string_view name)
    {
        for (const auto* model : std::array{ &norQspi16m, &norQspi1m, &internalFlash })
            if (model->name == name)
                return model;

        return nullptr;
    }

    const BlockDeviceModel* FindBlockDeviceModel(std::string_view name)
    {
        return sdCard256m.name == name ? &sdCard256m : nullptr;
    }

    infra::Duration ProgramTime(const NorModel& model, TimingCase timingCase, uint32_t address, std::size_t size)
    {
        if (size == 0)
            return {};

        auto firstPage = address / model.pageSize;
        auto lastPage = (address + size - 1) / model.pageSize;
        return model.Timing(timingCase).pageProgram * (lastPage - firstPage + 1);
    }

    infra::Duration ReadTime(const NorModel& model, TimingCase timingCase, std::size_t size)
    {
        auto rate = model.Timing(timingCase).readBytesPerSecond;
        auto nanoseconds = (static_cast<uint64_t>(size) * 1'000'000'000 + rate - 1) / rate;
        return std::chrono::duration_cast<infra::Duration>(std::chrono::nanoseconds(nanoseconds));
    }

    infra::Duration EraseTime(const NorModel& model, TimingCase timingCase, uint32_t sectors)
    {
        return model.Timing(timingCase).sectorErase * sectors;
    }

    infra::Duration ReadTime(const BlockDeviceModel& model, TimingCase timingCase, uint32_t blocks)
    {
        const auto& timing = model.Timing(timingCase);
        return timing.command + timing.blockRead * blocks;
    }

    infra::Duration WriteTime(const BlockDeviceModel& model, TimingCase timingCase, uint32_t blocks, uint32_t allocationUnitOpenings)
    {
        const auto& timing = model.Timing(timingCase);
        return timing.command + timing.writeBusy + timing.blockWritten * blocks + timing.allocationUnitOpening * allocationUnitOpenings;
    }

    infra::Duration CommandTime(const BlockDeviceModel& model, TimingCase timingCase)
    {
        return model.Timing(timingCase).command;
    }

    std::vector<std::pair<std::string, std::string>> Parameters(const NorModel& model)
    {
        std::vector<std::pair<std::string, std::string>> parameters{
            { "sectors", std::to_string(model.numberOfSectors) },
            { "sectorSize", std::to_string(model.sectorSize) },
            { "pageSize", std::to_string(model.pageSize) },
            { "programUnit", std::to_string(model.programUnit) },
        };

        AddTiming(parameters, "typical", model.typical);
        AddTiming(parameters, "maximum", model.maximum);
        return parameters;
    }

    std::vector<std::pair<std::string, std::string>> Parameters(const BlockDeviceModel& model)
    {
        std::vector<std::pair<std::string, std::string>> parameters{
            { "blockSize", std::to_string(model.blockSize) },
            { "blocks", std::to_string(model.numberOfBlocks) },
            { "blocksPerAllocationUnit", std::to_string(model.blocksPerAllocationUnit) },
            { "recentAllocationUnits", std::to_string(model.recentAllocationUnits) },
        };

        AddTiming(parameters, "typical", model.typical);
        AddTiming(parameters, "maximum", model.maximum);
        return parameters;
    }
}
