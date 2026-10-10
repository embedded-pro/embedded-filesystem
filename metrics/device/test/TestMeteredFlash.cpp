#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::NorModel testNor{ "test-nor", 4, 1024, 256, 1, { 400us, 45ms, 50'000'000 }, { 3ms, 400ms, 50'000'000 } };

    class MeteredFlashTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        void Write(uint32_t address, std::size_t size)
        {
            std::vector<uint8_t> data(size, 0x00);
            metered.WriteBuffer(infra::MakeRange(data), address, [] {});
            ForwardTime(10s);
        }

        void Erase(uint32_t beginIndex, uint32_t endIndex)
        {
            metered.EraseSectors(beginIndex, endIndex, [] {});
            ForwardTime(10s);
        }

        hal::FlashStub flash{ 4, 1024 };
        metrics::MeteredFlash metered{ flash, testNor };
        testing::StrictMock<infra::MockCallback<void()>> done;
    };
}

TEST_F(MeteredFlashTest, reports_the_geometry_of_the_wrapped_flash)
{
    EXPECT_EQ(4, metered.NumberOfSectors());
    EXPECT_EQ(1024, metered.SizeOfSector(1));
    EXPECT_EQ(2, metered.SectorOfAddress(2048));
    EXPECT_EQ(3072, metered.AddressOfSector(3));
}

TEST_F(MeteredFlashTest, write_is_forwarded_to_the_wrapped_flash)
{
    std::array<uint8_t, 2> data{ 1, 2 };

    metered.WriteBuffer(infra::MakeRange(data), 10, [] {});
    ForwardTime(1ms);

    EXPECT_EQ(1, flash.sectors[0][10]);
    EXPECT_EQ(2, flash.sectors[0][11]);
}

TEST_F(MeteredFlashTest, write_is_completed_after_one_page_program_time)
{
    std::array<uint8_t, 256> data{};

    metered.WriteBuffer(infra::MakeRange(data), 0, [this]
        {
            done.callback();
        });
    ExecuteAllActions();

    EXPECT_CALL(done, callback()).With(After(400us));
    ForwardTime(1ms);
}

TEST_F(MeteredFlashTest, write_spanning_two_pages_takes_two_page_program_times)
{
    std::array<uint8_t, 2> data{};

    EXPECT_CALL(done, callback()).With(After(800us));
    metered.WriteBuffer(infra::MakeRange(data), 255, [this]
        {
            done.callback();
        });
    ForwardTime(1ms);
}

TEST_F(MeteredFlashTest, maximum_timing_delays_a_write_by_the_maximum_page_program_time)
{
    metrics::MeteredFlash maximum{ flash, testNor, metrics::TimingCase::maximum };
    std::array<uint8_t, 1> data{};

    EXPECT_CALL(done, callback()).With(After(3ms));
    maximum.WriteBuffer(infra::MakeRange(data), 0, [this]
        {
            done.callback();
        });
    ForwardTime(5ms);
}

TEST_F(MeteredFlashTest, read_returns_the_data_of_the_wrapped_flash_after_the_transfer_time)
{
    flash.sectors[0][0] = 7;
    flash.sectors[0][3] = 9;
    std::array<uint8_t, 4> data{};

    EXPECT_CALL(done, callback()).With(After(80ns));
    metered.ReadBuffer(infra::MakeRange(data), 0, [this]
        {
            done.callback();
        });
    ForwardTime(1ms);

    EXPECT_EQ((std::array<uint8_t, 4>{ 7, 0xff, 0xff, 9 }), data);
}

TEST_F(MeteredFlashTest, erase_of_three_sectors_takes_three_sector_erase_times)
{
    flash.sectors[1][0] = 0;
    flash.sectors[3][0] = 0;

    EXPECT_CALL(done, callback()).With(After(135ms));
    metered.EraseSectors(1, 4, [this]
        {
            done.callback();
        });
    ForwardTime(200ms);

    EXPECT_EQ(0xff, flash.sectors[1][0]);
    EXPECT_EQ(0xff, flash.sectors[3][0]);
}

TEST_F(MeteredFlashTest, completion_waits_for_the_completion_of_the_wrapped_flash)
{
    flash.delaySignalEraseDone = true;

    metered.EraseSectors(0, 1, [this]
        {
            done.callback();
        });
    ForwardTime(1s);

    EXPECT_CALL(done, callback()).With(After(45ms));
    flash.onEraseDone();
    ForwardTime(100ms);
}

TEST_F(MeteredFlashTest, a_completion_may_start_the_next_request)
{
    std::array<uint8_t, 1> data{};

    metered.WriteBuffer(infra::MakeRange(data), 0, [this, &data]
        {
            metered.ReadBuffer(infra::MakeRange(data), 0, [this]
                {
                    done.callback();
                });
        });

    EXPECT_CALL(done, callback());
    ForwardTime(1ms);
}

TEST_F(MeteredFlashTest, counts_requests_and_bytes_of_every_kind)
{
    std::array<uint8_t, 8> data{};

    Write(0, 300);
    metered.ReadBuffer(infra::MakeRange(data), 0, [] {});
    ForwardTime(1ms);
    Erase(0, 2);

    EXPECT_EQ(1, metered.Counters().programRequests);
    EXPECT_EQ(300, metered.Counters().bytesProgrammed);
    EXPECT_EQ(1, metered.Counters().readRequests);
    EXPECT_EQ(8, metered.Counters().bytesRead);
    EXPECT_EQ(1, metered.Counters().eraseRequests);
    EXPECT_EQ(2, metered.Counters().sectorsErased);
}

TEST_F(MeteredFlashTest, accumulates_device_time_per_kind_of_request)
{
    std::array<uint8_t, 4> data{};

    Write(0, 1);
    metered.ReadBuffer(infra::MakeRange(data), 0, [] {});
    ForwardTime(1ms);
    Erase(1, 2);

    EXPECT_EQ(400us, metered.Counters().programTime);
    EXPECT_EQ(80ns, metered.Counters().readTime);
    EXPECT_EQ(45ms, metered.Counters().eraseTime);
    EXPECT_EQ(400us + 80ns + 45ms, metered.Counters().DeviceTime());
}

TEST_F(MeteredFlashTest, counts_erases_per_sector)
{
    Erase(1, 3);
    Erase(2, 3);

    EXPECT_EQ((std::vector<uint32_t>{ 0, 1, 2, 0 }), metered.EraseCounts());
}

TEST_F(MeteredFlashTest, counts_programming_a_program_unit_again_without_an_erase)
{
    Write(10, 2);
    Write(11, 2);

    EXPECT_EQ(1, metered.Counters().programUnitRewrites);
}

TEST_F(MeteredFlashTest, an_erase_resets_the_programmed_state_of_its_sectors)
{
    Write(10, 1);
    Erase(0, 1);
    Write(10, 1);

    EXPECT_EQ(0, metered.Counters().programUnitRewrites);
}

TEST_F(MeteredFlashTest, a_second_request_while_one_is_in_flight_aborts)
{
    std::array<uint8_t, 1> data{};

    EXPECT_DEATH(
        {
            metered.WriteBuffer(infra::MakeRange(data), 0, [] {});
            metered.WriteBuffer(infra::MakeRange(data), 1, [] {});
        },
        "");
}

TEST_F(MeteredFlashTest, a_write_beyond_the_end_of_the_flash_aborts)
{
    std::array<uint8_t, 2> data{};

    EXPECT_DEATH(metered.WriteBuffer(infra::MakeRange(data), 4095, [] {}), "");
}
