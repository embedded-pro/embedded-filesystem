#ifndef METRICS_DEVICE_DEVICE_MODELS_HPP
#define METRICS_DEVICE_DEVICE_MODELS_HPP

#include "infra/timer/TimerService.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace metrics
{
    enum class TimingCase : uint8_t
    {
        typical,
        maximum
    };

    struct NorTiming
    {
        infra::Duration pageProgram;
        infra::Duration sectorErase;
        uint64_t readBytesPerSecond;
    };

    struct NorModel
    {
        std::string_view name;
        uint32_t numberOfSectors;
        uint32_t sectorSize;
        uint32_t pageSize;
        uint32_t programUnit;
        NorTiming typical;
        NorTiming maximum;

        const NorTiming& Timing(TimingCase timingCase) const;
    };

    struct BlockDeviceTiming
    {
        infra::Duration command;
        infra::Duration blockRead;
        infra::Duration blockWritten;
        infra::Duration writeBusy;
        infra::Duration allocationUnitOpening;
    };

    struct BlockDeviceModel
    {
        std::string_view name;
        uint32_t blockSize;
        uint32_t numberOfBlocks;
        uint32_t blocksPerAllocationUnit;
        uint32_t recentAllocationUnits;
        BlockDeviceTiming typical;
        BlockDeviceTiming maximum;

        const BlockDeviceTiming& Timing(TimingCase timingCase) const;
    };

    extern const NorModel norQspi16m;
    extern const NorModel norQspi1m;
    extern const NorModel internalFlash;
    extern const BlockDeviceModel sdCard256m;

    const NorModel* FindNorModel(std::string_view name);
    const BlockDeviceModel* FindBlockDeviceModel(std::string_view name);

    infra::Duration ProgramTime(const NorModel& model, TimingCase timingCase, uint32_t address, std::size_t size);
    infra::Duration ReadTime(const NorModel& model, TimingCase timingCase, std::size_t size);
    infra::Duration EraseTime(const NorModel& model, TimingCase timingCase, uint32_t sectors);

    infra::Duration ReadTime(const BlockDeviceModel& model, TimingCase timingCase, uint32_t blocks);
    infra::Duration WriteTime(const BlockDeviceModel& model, TimingCase timingCase, uint32_t blocks, uint32_t allocationUnitOpenings);
    infra::Duration CommandTime(const BlockDeviceModel& model, TimingCase timingCase);

    std::vector<std::pair<std::string, std::string>> Parameters(const NorModel& model);
    std::vector<std::pair<std::string, std::string>> Parameters(const BlockDeviceModel& model);
}

#endif
