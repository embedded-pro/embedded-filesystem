#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/hil/FirmwareCommands.hpp"
#include "gmock/gmock.h"
#include <optional>

class FirmwareCommandsTest
    : public services::HilFixture
{
public:
    void Construct(bool cortexM4Stopped)
    {
        commands.emplace(context, validation::FirmwareInfo{ "0123abc", cortexM4Stopped });
    }

    std::optional<validation::FirmwareCommands> commands;
};

TEST_F(FirmwareCommandsTest, info_names_the_commit_and_a_parked_cortex_m4)
{
    Construct(true);

    Execute("firmware.info");

    EXPECT_EQ("OK commit=0123abc cm4=stopped\r\n", Output());
}

TEST_F(FirmwareCommandsTest, info_reports_a_cortex_m4_that_did_not_stop)
{
    Construct(false);

    Execute("firmware.info");

    EXPECT_EQ("OK commit=0123abc cm4=running\r\n", Output());
}

TEST_F(FirmwareCommandsTest, info_takes_no_arguments)
{
    Construct(true);

    Execute("firmware.info now");

    EXPECT_EQ("ERR usage\r\n", Output());
}
