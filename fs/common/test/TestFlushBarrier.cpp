#include "fs/common/FlushBarrier.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceMock.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    class FlushBarrierTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Write(uint32_t block, uint8_t rank)
        {
            barrier.Write(infra::MakeRange(data), block, rank, [this](hal::BlockDevice::Result result)
                {
                    done.callback(result);
                });
        }

        void Flush()
        {
            barrier.Flush([this](hal::BlockDevice::Result result)
                {
                    done.callback(result);
                });
        }

        void ExpectWrite(uint32_t block)
        {
            EXPECT_CALL(device, WriteBlocks(testing::_, block, testing::_)).WillOnce(testing::SaveArg<2>(&deviceDone));
        }

        void ExpectFlush()
        {
            EXPECT_CALL(device, Flush(testing::_)).WillOnce(testing::SaveArg<0>(&deviceDone));
        }

        void CompleteDevice(hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            ASSERT_TRUE(static_cast<bool>(deviceDone));
            auto onDone = deviceDone;
            deviceDone = nullptr;
            onDone(result);
        }

        void Written(uint32_t block, uint8_t rank)
        {
            ExpectWrite(block);
            Write(block, rank);
            ExpectDone();
        }

        void ExpectDone(hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            EXPECT_CALL(done, callback(result));
            CompleteDevice(result);
        }

        testing::StrictMock<hal::BlockDeviceMock> device;
        fs::FlushBarrier barrier{ device };
        std::array<uint8_t, 4> data{};
        infra::Function<void(hal::BlockDevice::Result)> deviceDone;
        testing::StrictMock<infra::MockCallback<void(hal::BlockDevice::Result)>> done;
    };
}

TEST_F(FlushBarrierTest, first_write_does_not_flush)
{
    Written(5, 2);

    EXPECT_TRUE(barrier.HasUnflushedWrites());
}

TEST_F(FlushBarrierTest, writes_of_one_rank_do_not_flush)
{
    Written(5, 1);
    Written(6, 1);
}

TEST_F(FlushBarrierTest, write_of_a_lower_rank_does_not_flush)
{
    Written(5, 2);
    Written(6, 0);
}

TEST_F(FlushBarrierTest, write_of_a_higher_rank_flushes_first)
{
    Written(5, 0);

    testing::InSequence sequence;
    ExpectFlush();
    ExpectWrite(6);
    Write(6, 1);
    CompleteDevice();
    ExpectDone();
}

TEST_F(FlushBarrierTest, flush_clears_the_barrier)
{
    Written(5, 0);
    ExpectFlush();
    Flush();
    ExpectDone();

    EXPECT_FALSE(barrier.HasUnflushedWrites());
    Written(6, 2);
}

TEST_F(FlushBarrierTest, lowest_unflushed_rank_decides)
{
    Written(5, 2);
    Written(6, 0);

    testing::InSequence sequence;
    ExpectFlush();
    ExpectWrite(7);
    Write(7, 1);
    CompleteDevice();
    ExpectDone();
}

TEST_F(FlushBarrierTest, flush_without_unflushed_writes_needs_no_device_request)
{
    Flush();
    testing::Mock::VerifyAndClearExpectations(&done);

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    ExecuteAllActions();
}

TEST_F(FlushBarrierTest, failed_flush_before_a_write_fails_the_write)
{
    Written(5, 0);
    ExpectFlush();
    Write(6, 1);

    ExpectDone(hal::BlockDevice::Result::timeout);
    EXPECT_TRUE(barrier.HasUnflushedWrites());
}

TEST_F(FlushBarrierTest, failed_flush_keeps_the_writes_unflushed)
{
    Written(5, 0);
    ExpectFlush();
    Flush();

    ExpectDone(hal::BlockDevice::Result::failed);
    EXPECT_TRUE(barrier.HasUnflushedWrites());
}

TEST_F(FlushBarrierTest, failed_write_counts_as_unflushed)
{
    ExpectWrite(5);
    Write(5, 0);
    ExpectDone(hal::BlockDevice::Result::crcError);

    EXPECT_TRUE(barrier.HasUnflushedWrites());
}

TEST_F(FlushBarrierTest, passes_the_data_and_exposes_the_device)
{
    EXPECT_CALL(device, WriteBlocks(testing::_, 9, testing::_)).WillOnce([this](infra::ConstByteRange written, uint32_t, const infra::Function<void(hal::BlockDevice::Result)>& onDone)
        {
            EXPECT_EQ(data.data(), written.begin());
            EXPECT_EQ(data.size(), written.size());
            deviceDone = onDone;
        });
    Write(9, 0);
    ExpectDone();

    EXPECT_EQ(&device, &barrier.Device());
}
