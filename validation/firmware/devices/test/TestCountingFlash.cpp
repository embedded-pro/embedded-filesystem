#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

namespace
{
    constexpr validation::FlashGuard guard{ 1ms, 2ms, 10ms };

    class CountingFlashTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        infra::Function<void()> Done()
        {
            return [this]
            {
                done.callback();
            };
        }

        void Complete()
        {
            EXPECT_CALL(done, callback());
            pending();
        }

        testing::StrictMock<hal::CleanFlashMock> flash;
        validation::FakeCycleCounter cycles;
        validation::CountingFlash counting{ flash, 16, cycles, validation::FakeCycleCounter::clockHz, guard };
        testing::StrictMock<infra::MockCallback<void()>> done;
        testing::StrictMock<infra::MockCallback<void()>> timedOut;
        infra::Function<void()> pending;
        std::array<uint8_t, 64> buffer{};
    };
}

TEST_F(CountingFlashTest, has_the_geometry_of_the_flash)
{
    EXPECT_CALL(flash, NumberOfSectors()).WillOnce(testing::Return(8));
    EXPECT_CALL(flash, SizeOfSector(3)).WillOnce(testing::Return(256));
    EXPECT_CALL(flash, SectorOfAddress(600)).WillOnce(testing::Return(2));
    EXPECT_CALL(flash, AddressOfSector(2)).WillOnce(testing::Return(512));

    EXPECT_EQ(8, counting.NumberOfSectors());
    EXPECT_EQ(256, counting.SizeOfSector(3));
    EXPECT_EQ(2, counting.SectorOfAddress(600));
    EXPECT_EQ(512, counting.AddressOfSector(2));
    EXPECT_EQ(16, counting.PageSize());
}

TEST_F(CountingFlashTest, counts_a_read_its_bytes_and_the_time_until_it_completes)
{
    EXPECT_CALL(flash, ReadBuffer(testing::_, 32, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBuffer(infra::MakeRange(buffer), 32, Done());
    EXPECT_TRUE(counting.Busy());

    ForwardTime(300us);
    Complete();

    EXPECT_FALSE(counting.Busy());
    EXPECT_EQ(1, counting.Counts().readRequests);
    EXPECT_EQ(64, counting.Counts().bytesRead);
    EXPECT_EQ(300'000, counting.Counts().readNs);
    EXPECT_EQ(300, counting.LastCycles());
}

TEST_F(CountingFlashTest, counts_a_program_its_bytes_and_the_pages_it_spans)
{
    EXPECT_CALL(flash, WriteBuffer(testing::_, 10, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.WriteBuffer(infra::Head(infra::MakeRange(buffer), 20), 10, Done());

    ForwardTime(500us);
    Complete();

    EXPECT_EQ(1, counting.Counts().programRequests);
    EXPECT_EQ(20, counting.Counts().bytesProgrammed);
    EXPECT_EQ(2, counting.Counts().pagesProgrammed);
    EXPECT_EQ(500'000, counting.Counts().programNs);
}

TEST_F(CountingFlashTest, an_empty_program_spans_no_page)
{
    EXPECT_CALL(flash, WriteBuffer(testing::_, 16, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.WriteBuffer(infra::ConstByteRange(), 16, Done());
    Complete();

    EXPECT_EQ(1, counting.Counts().programRequests);
    EXPECT_EQ(0, counting.Counts().pagesProgrammed);
}

TEST_F(CountingFlashTest, counts_an_erase_and_its_sectors)
{
    EXPECT_CALL(flash, EraseSectors(2, 5, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.EraseSectors(2, 5, Done());

    ForwardTime(7ms);
    Complete();

    EXPECT_EQ(1, counting.Counts().eraseRequests);
    EXPECT_EQ(3, counting.Counts().sectorsErased);
    EXPECT_EQ(7'000'000, counting.Counts().eraseNs);
}

TEST_F(CountingFlashTest, measures_a_request_across_the_wrap_of_the_cycle_counter)
{
    cycles.added = 0xffffff00;
    EXPECT_CALL(flash, ReadBuffer(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBuffer(infra::MakeRange(buffer), 0, Done());

    ForwardTime(512us);
    Complete();

    EXPECT_EQ(512, counting.LastCycles());
    EXPECT_EQ(512'000, counting.Counts().readNs);
}

TEST_F(CountingFlashTest, reports_a_request_that_outlasts_its_guard_and_still_counts_its_completion)
{
    counting.OnTimeout([this]
        {
            timedOut.callback();
        });
    EXPECT_CALL(flash, ReadBuffer(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBuffer(infra::MakeRange(buffer), 0, Done());

    ForwardTime(999us);
    EXPECT_CALL(timedOut, callback());
    ForwardTime(1us);
    EXPECT_TRUE(counting.Busy());

    ForwardTime(1ms);
    Complete();
    EXPECT_EQ(2'000'000, counting.Counts().readNs);
}

TEST_F(CountingFlashTest, the_guard_of_a_program_grows_with_its_pages_and_that_of_an_erase_with_its_sectors)
{
    counting.OnTimeout([this]
        {
            timedOut.callback();
        });
    EXPECT_CALL(flash, WriteBuffer(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.WriteBuffer(infra::Head(infra::MakeRange(buffer), 48), 0, Done());

    ForwardTime(5ms);
    EXPECT_CALL(timedOut, callback());
    ForwardTime(1ms);
    Complete();

    EXPECT_CALL(flash, EraseSectors(0, 2, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.EraseSectors(0, 2, Done());
    ForwardTime(19ms);
    EXPECT_CALL(timedOut, callback());
    ForwardTime(1ms);
    Complete();
}

TEST_F(CountingFlashTest, a_completion_within_the_guard_reports_no_timeout)
{
    counting.OnTimeout([this]
        {
            timedOut.callback();
        });
    EXPECT_CALL(flash, ReadBuffer(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBuffer(infra::MakeRange(buffer), 0, Done());
    Complete();

    ForwardTime(1s);
}

TEST_F(CountingFlashTest, a_timeout_without_an_action_is_only_ignored)
{
    EXPECT_CALL(flash, ReadBuffer(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBuffer(infra::MakeRange(buffer), 0, Done());

    ForwardTime(1s);
    Complete();
}

TEST_F(CountingFlashTest, a_flash_that_completes_within_the_call_can_be_asked_again_from_the_completion)
{
    EXPECT_CALL(flash, ReadBuffer(testing::_, testing::_, testing::_)).Times(2).WillRepeatedly([](infra::ByteRange, uint32_t, const infra::Function<void()>& onDone)
        {
            onDone();
        });

    EXPECT_CALL(done, callback()).Times(2);
    counting.ReadBuffer(infra::MakeRange(buffer), 0, [this]
        {
            done.callback();
            EXPECT_FALSE(counting.Busy());
            counting.ReadBuffer(infra::MakeRange(buffer), 64, Done());
        });

    EXPECT_FALSE(counting.Busy());
    EXPECT_EQ(2, counting.Counts().readRequests);
    EXPECT_EQ(0, counting.Counts().readNs);
}

TEST(FlashCountsTest, subtracts_and_adds_every_count)
{
    validation::FlashCounts later{ 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
    validation::FlashCounts earlier{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    EXPECT_EQ((validation::FlashCounts{ 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 }), later - earlier);
    EXPECT_EQ((validation::FlashCounts{ 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 }), later + earlier);
}

TEST_F(CountingFlashTest, is_in_use_while_a_request_is_pending_or_a_timeout_is_watched)
{
    EXPECT_FALSE(counting.InUse());
    counting.OnTimeout([] {});
    EXPECT_TRUE(counting.InUse());
    counting.OnTimeout(nullptr);
    EXPECT_FALSE(counting.InUse());

    EXPECT_CALL(flash, ReadBuffer(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBuffer(infra::MakeRange(buffer), 0, Done());
    EXPECT_TRUE(counting.InUse());
    Complete();

    EXPECT_FALSE(counting.InUse());
}

TEST_F(CountingFlashTest, a_timeout_action_may_replace_itself)
{
    counting.OnTimeout([this]
        {
            counting.OnTimeout(nullptr);
            timedOut.callback();
        });
    EXPECT_CALL(flash, ReadBuffer(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBuffer(infra::MakeRange(buffer), 0, Done());

    EXPECT_CALL(timedOut, callback());
    ForwardTime(1ms);
    Complete();

    EXPECT_FALSE(counting.InUse());
}
