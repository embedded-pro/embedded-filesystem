#ifndef METRICS_BOARDS_BOARD_DEVICES_HPP
#define METRICS_BOARDS_BOARD_DEVICES_HPP

#include "fs/nor/NorConfig.hpp"
#include "metrics/device/DeviceModels.hpp"
#include <array>
#include <span>
#include <string>
#include <string_view>

namespace metrics
{
    struct NorBoardDevice
    {
        std::string_view name;
        const NorModel* model;
        fs::NorConfig config;
    };

    extern const std::array<NorBoardDevice, 2> norBoardDevices;

    std::string DeviceModelsJson(std::span<const NorBoardDevice> devices);
}

#endif
