#include "fs/fat/FatFormatter.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include "validation/firmware/devices/test_doubles/CountedDeviceMock.hpp"
#include "validation/firmware/layout/LayoutCommands.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>

using namespace std::chrono_literals;

namespace
{
    constexpr uint32_t cardBlocks = 8192;

    std::array<uint8_t, cardBlocks * validation::RamBlockDevice::blockSize>& CardMemory()
    {
        static std::array<uint8_t, cardBlocks * validation::RamBlockDevice::blockSize> memory;
        return memory;
    }

    class LayoutCommandsTest
        : public services::HilFixture
    {
    public:
        LayoutCommandsTest()
        {
            CardMemory().fill(0);
            EXPECT_CALL(mockCard, BlockSize()).WillRepeatedly(testing::Return(512));
            EXPECT_CALL(mockCard, NumberOfBlocks()).WillRepeatedly(testing::Return(cardBlocks));
        }

        void ExpectClaimed()
        {
            EXPECT_CALL(claim, InUse()).WillOnce(testing::Return(false));
            EXPECT_CALL(claim, OnTimeout(testing::_)).Times(2);
        }

        void FormatCard()
        {
            fs::FatFormatter::WithStorage<8> formatter{ card };
            formatter.Format({}, [](fs::Result) {});
            ExecuteAllActions();
        }

        void CompleteMock(hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            auto done = pending;
            done(result);
        }

        validation::RamBlockDevice card{ infra::MakeRange(CardMemory()) };
        testing::StrictMock<hal::BlockDeviceMock> mockCard;
        testing::StrictMock<validation::CountedDeviceMock> claim;
        std::array<validation::DeviceEntry, 3> devices{ {
            { { "sd", validation::DeviceKind::block, 0, 512, 0, "", 0, "" }, true },
            { { "nocard", validation::DeviceKind::block, 0, 512, 0, "", 0, "" }, false },
            { { "mock", validation::DeviceKind::block, 0, 512, 0, "", 0, "" }, true },
        } };
        const std::array<validation::LayoutTarget, 3> targets{ {
            { devices[0], card, 1024, claim },
            { devices[1], card, 0, claim },
            { devices[2], mockCard, 0, claim },
        } };
        validation::ResultLines results{ response };
        validation::LayoutCommands commands{ context, results, infra::MakeRange(targets) };
        infra::Function<void(hal::BlockDevice::Result)> pending;
    };
}

TEST_F(LayoutCommandsTest, read_prints_the_layout_of_the_card_and_of_the_formatter_and_their_differences)
{
    FormatCard();
    CardMemory()[512 * 11 + 13] = 8;
    ExpectClaimed();

    Execute("layout.read sd");
    ExecuteAllActions();

    auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr("EVT layout id=1 device=sd source=card blocks=8192 unitBlocks=1024 partitionStart=11 "));
    EXPECT_THAT(output, testing::HasSubstr(" type=fat12 sectorsPerCluster=8 "));
    EXPECT_THAT(output, testing::HasSubstr("EVT layout id=2 device=sd source=formatter blocks=8192 unitBlocks=1024 partitionStart=11 "));
    EXPECT_THAT(output, testing::HasSubstr(" type=fat12 sectorsPerCluster=16 "));
    EXPECT_THAT(output, testing::HasSubstr(" differs=sectorsPerCluster\r\n"));
    EXPECT_THAT(output, testing::EndsWith("OK differences=1\r\n"));
}

TEST_F(LayoutCommandsTest, format_formats_the_whole_card_without_discarding_and_its_layout_is_that_of_the_formatter)
{
    std::fill(CardMemory().end() - 512, CardMemory().end(), 0xa5);
    ExpectClaimed();

    Execute("layout.format sd");
    ExecuteAllActions();

    EXPECT_EQ("\r\nOK\r\n", Output());
    EXPECT_EQ(0xa5, CardMemory().back());

    ExpectClaimed();
    Execute("layout.read sd");
    ExecuteAllActions();
    auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr(" differs=\r\n"));
    EXPECT_THAT(output, testing::EndsWith("OK differences=0\r\n"));
}

TEST_F(LayoutCommandsTest, a_card_without_a_volume_answers_failed)
{
    ExpectClaimed();

    Execute("layout.read sd");
    ExecuteAllActions();

    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(LayoutCommandsTest, a_boot_sector_that_is_not_fat_answers_failed)
{
    FormatCard();
    CardMemory()[512 * 11 + 510] = 0;
    ExpectClaimed();

    Execute("layout.read sd");
    ExecuteAllActions();

    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(LayoutCommandsTest, a_read_that_fails_answers_failed)
{
    ExpectClaimed();
    EXPECT_CALL(mockCard, ReadBlocks(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));

    Execute("layout.read mock");
    CompleteMock(hal::BlockDevice::Result::crcError);

    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(LayoutCommandsTest, a_boot_sector_that_fails_to_read_answers_failed)
{
    FormatCard();
    ExpectClaimed();
    EXPECT_CALL(mockCard, ReadBlocks(testing::_, 0, testing::_)).WillOnce([this](infra::ByteRange buffer, uint32_t, const auto& onDone)
        {
            std::copy_n(CardMemory().begin(), 512, buffer.begin());
            pending = onDone;
        });
    EXPECT_CALL(mockCard, ReadBlocks(testing::_, 11, testing::_)).WillOnce(testing::SaveArg<2>(&pending));

    Execute("layout.read mock");
    CompleteMock();
    CompleteMock(hal::BlockDevice::Result::timeout);

    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(LayoutCommandsTest, a_card_too_small_for_the_formatter_answers_failed)
{
    FormatCard();
    ExpectClaimed();
    EXPECT_CALL(mockCard, NumberOfBlocks()).WillRepeatedly(testing::Return(16));
    EXPECT_CALL(mockCard, ReadBlocks(testing::_, testing::_, testing::_)).Times(2).WillRepeatedly([this](infra::ByteRange buffer, uint32_t block, const auto& onDone)
        {
            std::copy_n(CardMemory().begin() + 512 * block, 512, buffer.begin());
            pending = onDone;
        });

    Execute("layout.read mock");
    CompleteMock();
    CompleteMock();

    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(LayoutCommandsTest, a_format_that_fails_answers_failed)
{
    ExpectClaimed();
    EXPECT_CALL(mockCard, WriteBlocks(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));

    Execute("layout.format mock");
    CompleteMock(hal::BlockDevice::Result::writeProtected);
    ExecuteAllActions();

    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(LayoutCommandsTest, a_request_that_does_not_complete_answers_timeout_and_keeps_the_card_until_it_completes)
{
    ExpectClaimed();
    EXPECT_CALL(mockCard, ReadBlocks(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));

    Execute("layout.read mock");
    ForwardTime(validation::LayoutCommands::readTimeout);
    EXPECT_EQ("\r\nERR timeout\r\n", Output());

    Execute("layout.read sd");
    EXPECT_EQ("ERR busy\r\n", Output());

    CompleteMock();
    EXPECT_EQ("", Output());
}

TEST_F(LayoutCommandsTest, a_guard_of_the_region_that_expires_while_a_command_holds_the_card_changes_nothing)
{
    infra::Function<void()> onTimeout;
    EXPECT_CALL(claim, InUse()).WillOnce(testing::Return(false));
    EXPECT_CALL(claim, OnTimeout(testing::_)).WillOnce(testing::SaveArg<0>(&onTimeout)).WillOnce(testing::Return());
    EXPECT_CALL(mockCard, ReadBlocks(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));

    Execute("layout.read mock");
    onTimeout();
    EXPECT_EQ("", Output());

    CompleteMock(hal::BlockDevice::Result::crcError);
    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(LayoutCommandsTest, a_late_format_completion_releases_the_card)
{
    ExpectClaimed();
    EXPECT_CALL(mockCard, WriteBlocks(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));

    Execute("layout.format mock");
    ForwardTime(validation::LayoutCommands::formatTimeout);
    EXPECT_EQ("\r\nERR timeout\r\n", Output());

    CompleteMock(hal::BlockDevice::Result::failed);
    ExecuteAllActions();
    EXPECT_EQ("", Output());
}

TEST_F(LayoutCommandsTest, a_card_that_a_benchmark_or_a_workload_uses_is_busy)
{
    EXPECT_CALL(claim, InUse()).WillRepeatedly(testing::Return(true));

    for (const char* command : { "layout.read sd", "layout.format sd" })
    {
        Execute(command);
        EXPECT_EQ("ERR busy\r\n", Output()) << command;
    }
}

TEST_F(LayoutCommandsTest, commands_take_one_card_that_is_present)
{
    for (const char* command : { "layout.read", "layout.format sd mock", "layout.read sd n=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR usage\r\n", Output()) << command;
    }

    for (const char* command : { "layout.read nocard", "layout.format other" })
    {
        Execute(command);
        EXPECT_EQ("ERR unsupported\r\n", Output()) << command;
    }
}

TEST_F(LayoutCommandsTest, a_comparison_that_completes_after_its_timeout_prints_nothing)
{
    FormatCard();
    ExpectClaimed();
    EXPECT_CALL(mockCard, ReadBlocks(testing::_, testing::_, testing::_)).Times(2).WillRepeatedly([this](infra::ByteRange buffer, uint32_t block, const auto& onDone)
        {
            std::copy_n(CardMemory().begin() + 512 * block, 512, buffer.begin());
            pending = onDone;
        });

    Execute("layout.read mock");
    CompleteMock();
    ForwardTime(validation::LayoutCommands::readTimeout);
    EXPECT_EQ("\r\nERR timeout\r\n", Output());

    CompleteMock();
    EXPECT_EQ("", Output());
}
