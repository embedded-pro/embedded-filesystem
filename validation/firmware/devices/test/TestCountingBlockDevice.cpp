#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "validation/firmware/devices/CountingBlockDevice.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

namespace
{
    constexpr uint32_t unitBlocks = 8;

    class CountingBlockDeviceTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        CountingBlockDeviceTest()
        {
            EXPECT_CALL(device, BlockSize()).WillRepeatedly(testing::Return(512));
            EXPECT_CALL(device, NumberOfBlocks()).WillRepeatedly(testing::Return(64));
        }

        infra::Function<void(hal::BlockDevice::Result)> Done()
        {
            return [this](hal::BlockDevice::Result result)
            {
                done.callback(result);
            };
        }

        void Complete(hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            EXPECT_CALL(done, callback(result));
            pending(result);
        }

        void Write(uint32_t firstBlock, uint32_t blocks)
        {
            EXPECT_CALL(device, WriteBlocks(testing::SizeIs(blocks * 512), firstBlock, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
            counting.WriteBlocks(infra::Head(infra::MakeRange(buffer), blocks * 512), firstBlock, Done());
            Complete();
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        validation::FakeCycleCounter cycles;
        validation::CountingBlockDevice counting{ device, unitBlocks, cycles, validation::FakeCycleCounter::clockHz, 5ms };
        testing::StrictMock<infra::MockCallback<void(hal::BlockDevice::Result)>> done;
        testing::StrictMock<infra::MockCallback<void()>> timedOut;
        infra::Function<void(hal::BlockDevice::Result)> pending;
        std::array<uint8_t, 4 * 512> buffer{};
    };
}

TEST_F(CountingBlockDeviceTest, has_the_geometry_of_the_device)
{
    EXPECT_EQ(512, counting.BlockSize());
    EXPECT_EQ(64, counting.NumberOfBlocks());
    EXPECT_EQ(unitBlocks, counting.UnitBlocks());
}

TEST_F(CountingBlockDeviceTest, counts_a_read_its_blocks_and_the_time_until_it_completes)
{
    EXPECT_CALL(device, ReadBlocks(testing::_, 3, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBlocks(infra::Head(infra::MakeRange(buffer), 1024), 3, Done());
    EXPECT_TRUE(counting.Busy());

    ForwardTime(400us);
    Complete();

    EXPECT_FALSE(counting.Busy());
    EXPECT_EQ(1, counting.Counts().readRequests);
    EXPECT_EQ(2, counting.Counts().blocksRead);
    EXPECT_EQ(400'000, counting.Counts().readNs);
    EXPECT_EQ(400, counting.LastCycles());
}

TEST_F(CountingBlockDeviceTest, counts_a_write_its_blocks_and_the_time_until_it_completes)
{
    EXPECT_CALL(device, WriteBlocks(testing::_, 8, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.WriteBlocks(infra::MakeRange(buffer), 8, Done());

    ForwardTime(2ms);
    Complete();

    EXPECT_EQ(1, counting.Counts().writeRequests);
    EXPECT_EQ(4, counting.Counts().blocksWritten);
    EXPECT_EQ(2'000'000, counting.Counts().writeNs);
}

TEST_F(CountingBlockDeviceTest, counts_the_units_a_write_spans_by_their_place_among_the_four_most_recently_written)
{
    Write(0, 2);
    Write(8, 2);
    Write(2, 2);
    Write(6, 4);
    Write(16, 1);
    Write(24, 1);
    Write(32, 1);
    Write(40, 1);
    Write(17, 1);
    Write(3, 1);

    const auto& counts = counting.Counts();
    EXPECT_EQ(1, counts.unitsByRecency[0]);
    EXPECT_EQ(2, counts.unitsByRecency[1]);
    EXPECT_EQ(0, counts.unitsByRecency[2]);
    EXPECT_EQ(1, counts.unitsByRecency[3]);
    EXPECT_EQ(7, counts.unitsNotRecent);
    EXPECT_EQ(6, counts.distinctUnits);
}

TEST_F(CountingBlockDeviceTest, counts_written_blocks_that_start_a_unit_or_follow_the_block_written_before_them)
{
    Write(8, 2);
    Write(10, 3);
    Write(20, 2);
    Write(22, 1);
    Write(30, 4);

    EXPECT_EQ(2 + 3 + 1 + 1 + 3, counting.Counts().alignedBlocks);
}

TEST_F(CountingBlockDeviceTest, counts_flushes_and_discards_and_the_blocks_discarded)
{
    EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&pending));
    counting.Flush(Done());
    ForwardTime(1ms);
    Complete();

    EXPECT_CALL(device, EraseBlocks(8, 24, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.EraseBlocks(8, 24, Done());
    ForwardTime(3ms);
    Complete();

    const auto& counts = counting.Counts();
    EXPECT_EQ(1, counts.flushRequests);
    EXPECT_EQ(1'000'000, counts.flushNs);
    EXPECT_EQ(1, counts.discardRequests);
    EXPECT_EQ(16, counts.blocksDiscarded);
    EXPECT_EQ(3'000'000, counts.discardNs);
}

TEST_F(CountingBlockDeviceTest, counts_requests_that_fail)
{
    EXPECT_CALL(device, ReadBlocks(testing::_, 0, testing::_)).WillOnce(testing::SaveArg<2>(&pending));
    counting.ReadBlocks(infra::Head(infra::MakeRange(buffer), 512), 0, Done());
    Complete(hal::BlockDevice::Result::crcError);

    EXPECT_EQ(1, counting.Counts().failedRequests);
}

TEST_F(CountingBlockDeviceTest, a_request_past_its_guard_calls_the_timeout_handler_and_still_completes)
{
    counting.OnTimeout([this]
        {
            timedOut.callback();
        });
    EXPECT_TRUE(counting.InUse());

    EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&pending));
    counting.Flush(Done());
    EXPECT_CALL(timedOut, callback());
    ForwardTime(5ms);

    ForwardTime(1ms);
    Complete();
    EXPECT_EQ(6'000'000, counting.Counts().flushNs);

    counting.OnTimeout(nullptr);
    EXPECT_FALSE(counting.InUse());
}

TEST_F(CountingBlockDeviceTest, a_request_within_its_guard_does_not_time_out)
{
    counting.OnTimeout([this]
        {
            timedOut.callback();
        });

    EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&pending));
    counting.Flush(Done());
    ForwardTime(4ms);
    Complete();
    ForwardTime(10ms);
}

TEST(BlockCountsTest, counts_add_up_and_take_the_difference)
{
    validation::BlockCounts earlier;
    earlier.readRequests = 1;
    earlier.unitsByRecency[2] = 3;
    earlier.distinctUnits = 2;

    validation::BlockCounts later = earlier + earlier;
    EXPECT_EQ(2, later.readRequests);
    EXPECT_EQ(6, later.unitsByRecency[2]);
    EXPECT_EQ(4, later.distinctUnits);
    EXPECT_EQ(earlier, later - earlier);
}
