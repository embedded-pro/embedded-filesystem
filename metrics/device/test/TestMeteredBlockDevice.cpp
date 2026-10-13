#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "metrics/device/MeteredBlockDevice.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <map>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::BlockDeviceModel testBlock{ "test-block", 512, 64, 4, 2, { 100us, 25us, 50us, 0us, 10ms }, { 100us, 25us, 50us, 250ms, 10ms } };

    class MeteredBlockDeviceTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        void Write(uint32_t firstBlock, uint32_t blocks)
        {
            std::vector<uint8_t> data(blocks * 512, 0x5a);
            metered.WriteBlocks(infra::MakeRange(data), firstBlock, [](hal::BlockDevice::Result) {});
            ForwardTime(1s);
        }

        void ExpectWriteTakes(uint32_t firstBlock, uint32_t blocks, infra::Duration duration)
        {
            std::vector<uint8_t> data(blocks * 512, 0x5a);
            EXPECT_CALL(done, callback(hal::BlockDevice::Result::success)).With(After(duration));
            metered.WriteBlocks(infra::MakeRange(data), firstBlock, [this](hal::BlockDevice::Result result)
                {
                    done.callback(result);
                });
            ForwardTime(1s);
        }

        std::vector<uint8_t> storage = std::vector<uint8_t>(64 * 512, 0);
        hal::BlockDeviceStub stub{ infra::MakeRange(storage), 512 };
        metrics::MeteredBlockDevice metered{ stub, testBlock };
        testing::StrictMock<infra::MockCallback<void(hal::BlockDevice::Result)>> done;
    };
}

TEST_F(MeteredBlockDeviceTest, reports_the_block_size_and_number_of_blocks_of_the_wrapped_device)
{
    EXPECT_EQ(512, metered.BlockSize());
    EXPECT_EQ(64, metered.NumberOfBlocks());
}

TEST_F(MeteredBlockDeviceTest, read_is_completed_after_command_and_per_block_time)
{
    std::vector<uint8_t> data(2 * 512);

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success)).With(After(150us));
    metered.ReadBlocks(infra::MakeRange(data), 0, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    ForwardTime(1ms);
}

TEST_F(MeteredBlockDeviceTest, write_is_forwarded_and_completed_after_command_and_per_block_time)
{
    ExpectWriteTakes(1, 2, 200us);

    EXPECT_EQ(0x5a, storage[512]);
    EXPECT_EQ(0x5a, storage[3 * 512 - 1]);
}

TEST_F(MeteredBlockDeviceTest, completion_is_not_delivered_from_inside_the_call)
{
    std::vector<uint8_t> data(512);

    metered.WriteBlocks(infra::MakeRange(data), 0, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    ExecuteAllActions();

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    ForwardTime(1ms);
}

TEST_F(MeteredBlockDeviceTest, maximum_timing_adds_the_write_busy_time)
{
    metrics::MeteredBlockDevice maximum{ stub, testBlock, metrics::TimingCase::maximum };
    std::vector<uint8_t> data(512);

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success)).With(After(100us + 250ms + 50us));
    maximum.WriteBlocks(infra::MakeRange(data), 0, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    ForwardTime(1s);
}

TEST_F(MeteredBlockDeviceTest, write_into_one_of_the_two_most_recent_allocation_units_has_no_penalty)
{
    Write(0, 1);
    Write(4, 1);

    ExpectWriteTakes(1, 1, 150us);
}

TEST_F(MeteredBlockDeviceTest, write_opening_a_third_allocation_unit_adds_the_penalty)
{
    Write(0, 1);
    Write(4, 1);

    ExpectWriteTakes(8, 1, 150us + 10ms);
    EXPECT_EQ(1, metered.Counters().allocationUnitOpenings);
}

TEST_F(MeteredBlockDeviceTest, write_spanning_two_new_allocation_units_adds_two_penalties)
{
    Write(0, 1);
    Write(4, 1);

    ExpectWriteTakes(11, 2, 200us + 20ms);
}

TEST_F(MeteredBlockDeviceTest, returning_to_an_evicted_allocation_unit_adds_the_penalty)
{
    Write(0, 1);
    Write(4, 1);
    Write(8, 1);

    ExpectWriteTakes(0, 1, 150us + 10ms);
}

TEST_F(MeteredBlockDeviceTest, reads_do_not_change_the_recent_allocation_units)
{
    std::vector<uint8_t> data(512);
    Write(0, 1);
    Write(4, 1);
    metered.ReadBlocks(infra::MakeRange(data), 20, [](hal::BlockDevice::Result) {});
    ForwardTime(1ms);

    ExpectWriteTakes(0, 1, 150us);
}

TEST_F(MeteredBlockDeviceTest, a_failed_request_is_forwarded_and_takes_command_time_only)
{
    std::vector<uint8_t> data(2 * 512);
    stub.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::timeout)).With(After(100us));
    metered.WriteBlocks(infra::MakeRange(data), 0, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    ForwardTime(1ms);

    EXPECT_EQ(1, metered.Counters().failedRequests);
    EXPECT_EQ(0, metered.Counters().blocksWritten);
}

TEST_F(MeteredBlockDeviceTest, flush_is_forwarded_and_completed_after_command_time)
{
    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success)).With(After(100us));
    metered.Flush([this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    ForwardTime(1ms);

    EXPECT_EQ(1, metered.Counters().flushRequests);
    EXPECT_EQ(100us, metered.Counters().flushTime);
}

TEST_F(MeteredBlockDeviceTest, discard_is_forwarded_and_completed_after_command_time)
{
    Write(0, 4);

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success)).With(After(100us));
    metered.EraseBlocks(0, 4, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    ForwardTime(1ms);

    EXPECT_EQ(0, storage[0]);
    EXPECT_EQ(1, metered.Counters().discardRequests);
    EXPECT_EQ(4, metered.Counters().blocksDiscarded);
    EXPECT_EQ(100us, metered.Counters().discardTime);
}

TEST_F(MeteredBlockDeviceTest, counts_requests_and_blocks_of_every_kind)
{
    std::vector<uint8_t> data(3 * 512);
    Write(0, 2);
    metered.ReadBlocks(infra::MakeRange(data), 0, [](hal::BlockDevice::Result) {});
    ForwardTime(1ms);

    EXPECT_EQ(1, metered.Counters().writeRequests);
    EXPECT_EQ(2, metered.Counters().blocksWritten);
    EXPECT_EQ(1, metered.Counters().readRequests);
    EXPECT_EQ(3, metered.Counters().blocksRead);
    EXPECT_EQ(200us, metered.Counters().writeTime);
    EXPECT_EQ(175us, metered.Counters().readTime);
}

TEST_F(MeteredBlockDeviceTest, records_a_histogram_of_blocks_per_write_request)
{
    Write(0, 1);
    Write(1, 1);
    Write(2, 3);

    EXPECT_EQ((std::map<uint32_t, uint64_t>{ { 1, 2 }, { 3, 1 } }), metered.BlocksPerWriteHistogram());
}

TEST_F(MeteredBlockDeviceTest, the_blocks_of_a_write_starting_on_an_allocation_unit_boundary_are_aligned)
{
    Write(8, 3);

    EXPECT_EQ(3, metered.Counters().alignedBlocks);
}

TEST_F(MeteredBlockDeviceTest, the_blocks_of_a_write_continuing_the_previous_write_are_aligned)
{
    Write(1, 1);
    Write(2, 2);

    EXPECT_EQ(2, metered.Counters().alignedBlocks);
}

TEST_F(MeteredBlockDeviceTest, only_the_first_block_of_a_write_neither_on_a_boundary_nor_continuing_is_not_aligned)
{
    Write(1, 1);
    Write(6, 3);

    EXPECT_EQ(2, metered.Counters().alignedBlocks);
}

TEST_F(MeteredBlockDeviceTest, an_empty_write_has_no_aligned_blocks)
{
    Write(1, 0);

    EXPECT_EQ(0, metered.Counters().alignedBlocks);
}

TEST_F(MeteredBlockDeviceTest, counts_writes_per_allocation_unit)
{
    Write(3, 2);
    Write(5, 1);

    EXPECT_EQ(16, metered.WritesPerAllocationUnit().size());
    EXPECT_EQ(1, metered.WritesPerAllocationUnit()[0]);
    EXPECT_EQ(2, metered.WritesPerAllocationUnit()[1]);
    EXPECT_EQ(0, metered.WritesPerAllocationUnit()[2]);
}

TEST_F(MeteredBlockDeviceTest, a_second_request_while_one_is_in_flight_aborts)
{
    std::vector<uint8_t> data(512);

    EXPECT_DEATH(
        {
            metered.WriteBlocks(infra::MakeRange(data), 0, [](hal::BlockDevice::Result) {});
            metered.Flush([](hal::BlockDevice::Result) {});
        },
        "");
}
