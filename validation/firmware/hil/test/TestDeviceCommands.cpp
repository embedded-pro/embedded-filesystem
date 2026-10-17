#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/hil/DeviceCommands.hpp"
#include "gmock/gmock.h"
#include <array>

class DeviceCommandsTest
    : public services::HilFixture
{
public:
    std::array<validation::DeviceEntry, 3> devices{ {
        { { "qspi", validation::DeviceKind::nor, 4096, 4096, 256, "20ba20", 50'000'000, "" }, true },
        { { "fmc", validation::DeviceKind::nor, 128, 131072, 64, "", 0, "" }, false },
        { { "ramblock", validation::DeviceKind::block, 4096, 512, 0, "", 0, "sdram" }, true },
    } };
    validation::ResultLines results{ response };
    validation::DeviceCommands commands{ context, results, infra::MakeRange(devices) };
};

TEST_F(DeviceCommandsTest, list_names_the_devices_that_are_present)
{
    Execute("dev.list");

    EXPECT_EQ("OK devices=qspi,ramblock\r\n", Output());
}

TEST_F(DeviceCommandsTest, list_without_devices_names_none)
{
    devices[0].present = false;
    devices[2].present = false;

    Execute("dev.list");

    EXPECT_EQ("OK devices=\r\n", Output());
}

TEST_F(DeviceCommandsTest, list_takes_no_arguments)
{
    Execute("dev.list qspi");

    EXPECT_THAT(Output(), testing::EndsWith("ERR usage\r\n"));
}

TEST_F(DeviceCommandsTest, info_describes_a_device)
{
    Execute("dev.info qspi");

    EXPECT_EQ("EVT device id=1 device=qspi kind=nor sectors=4096 sectorSize=4096 pageSize=256 chip=20ba20 clockHz=50000000\r\nOK\r\n", Output());
}

TEST_F(DeviceCommandsTest, info_on_a_device_that_is_absent_or_unknown_is_unsupported)
{
    Execute("dev.info fmc");
    EXPECT_EQ("ERR unsupported\r\n", Output());

    Execute("dev.info sd");
    EXPECT_EQ("ERR unsupported\r\n", Output());
}

TEST_F(DeviceCommandsTest, info_takes_one_device)
{
    Execute("dev.info");
    EXPECT_EQ("ERR usage\r\n", Output());

    Execute("dev.info qspi fmc");
    EXPECT_EQ("ERR usage\r\n", Output());
}

TEST_F(DeviceCommandsTest, finds_a_present_device_by_name)
{
    EXPECT_EQ(&devices[0], validation::FindDevice(infra::MakeRange(devices), "qspi"));
    EXPECT_EQ(nullptr, validation::FindDevice(infra::MakeRange(devices), "fmc"));
    EXPECT_EQ(nullptr, validation::FindDevice(infra::MakeRange(devices), "sd"));
}
