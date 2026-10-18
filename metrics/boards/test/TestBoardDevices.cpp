#include "metrics/boards/BoardDevices.hpp"
#include "metrics/fat/FatTargets.hpp"
#include "metrics/nor/NorConfigurations.hpp"
#include "metrics/nor/NorTargets.hpp"
#include "gmock/gmock.h"
#include <array>
#include <string>
#include <string_view>

TEST(BoardDevicesTest, json_names_the_model_of_each_device_with_its_parameters)
{
    std::array<metrics::NorBoardDevice, 1> devices{ { { "board/flash", &metrics::norQspi1m, metrics::norQspi1mConfig } } };

    auto json = metrics::DeviceModelsJson(devices);

    std::string expected = "{\n  \"board/flash\": { \"model\": \"nor-qspi-1m\"";
    for (const auto& [key, value] : metrics::Parameters(metrics::norQspi1m))
        expected += ", \"" + key + "\": \"" + value + "\"";
    EXPECT_THAT(json, testing::StartsWith(expected + ", \"w6BoundMs\": \""));
    EXPECT_THAT(json, testing::EndsWith("\" }\n}\n"));
}

TEST(BoardDevicesTest, json_gives_the_w6_bound_of_the_model_and_the_configuration)
{
    std::array<metrics::NorBoardDevice, 1> devices{ { { "board/flash", &metrics::norQspi1m, metrics::norQspi1mConfig } } };
    metrics::NorTargets targets{ metrics::norQspi1m, metrics::norQspi1mConfig, 1 };

    auto json = metrics::DeviceModelsJson(devices);

    std::string_view key{ "\"w6BoundMs\": \"" };
    auto start = json.find(key) + key.size();
    EXPECT_DOUBLE_EQ(targets.CommitMilliseconds(), std::stod(json.substr(start, json.find('"', start) - start)));
}

TEST(BoardDevicesTest, json_separates_the_devices)
{
    std::array<metrics::NorBoardDevice, 2> devices{ {
        { "a/flash", &metrics::norQspi1m, metrics::norQspi1mConfig },
        { "b/flash", &metrics::norQspi1m, metrics::norQspi1mConfig },
    } };

    auto json = metrics::DeviceModelsJson(devices);

    EXPECT_THAT(json, testing::HasSubstr(" },\n  \"b/flash\": { "));
}

TEST(BoardDevicesTest, empty_json_is_an_empty_object)
{
    EXPECT_EQ("{\n}\n", metrics::DeviceModelsJson({}));
}

TEST(BoardDevicesTest, devices_of_the_boards_take_the_geometry_of_their_model)
{
    for (const auto& device : metrics::norBoardDevices)
    {
        EXPECT_EQ(device.model->numberOfSectors, device.config.maxBlocks * device.config.sectorsPerBlock) << device.name;
        EXPECT_EQ(device.model->programUnit, device.config.programUnit) << device.name;
        EXPECT_EQ(device.model->pageSize, device.config.pageSize) << device.name;
    }
}

TEST(BoardDevicesTest, json_gives_a_block_device_its_model_and_the_w6_bound_of_fat)
{
    std::array<metrics::BlockBoardDevice, 1> devices{ { { "board/card", &metrics::sdCard256m } } };
    metrics::FatTargets targets{ metrics::sdCard256m };

    auto json = metrics::DeviceModelsJson({}, devices);

    std::string expected = "{\n  \"board/card\": { \"model\": \"sd-card-256m\"";
    for (const auto& [key, value] : metrics::Parameters(metrics::sdCard256m))
        expected += ", \"" + key + "\": \"" + value + "\"";
    EXPECT_THAT(json, testing::StartsWith(expected + ", \"w6BoundMs\": \""));
    std::string_view key{ "\"w6BoundMs\": \"" };
    auto start = json.find(key) + key.size();
    EXPECT_DOUBLE_EQ(targets.CommitMilliseconds(), std::stod(json.substr(start, json.find('"', start) - start)));
}

TEST(BoardDevicesTest, json_lists_the_block_devices_after_the_nor_devices)
{
    std::array<metrics::NorBoardDevice, 1> norDevices{ { { "a/flash", &metrics::norQspi1m, metrics::norQspi1mConfig } } };
    std::array<metrics::BlockBoardDevice, 1> blockDevices{ { { "a/card", &metrics::sdCard256m } } };

    auto json = metrics::DeviceModelsJson(norDevices, blockDevices);

    EXPECT_THAT(json, testing::HasSubstr(" },\n  \"a/card\": { "));
}

TEST(BoardDevicesTest, block_devices_of_the_boards_are_regions_of_256_mib_with_the_geometry_of_their_model)
{
    for (const auto& device : metrics::blockBoardDevices)
        EXPECT_EQ(256ull * 1024 * 1024, uint64_t{ device.model->numberOfBlocks } * device.model->blockSize) << device.name;
}
