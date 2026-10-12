#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    using Block = std::array<uint8_t, 16>;

    class SparseBlockDeviceTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Write(const Block& block, uint32_t number, hal::BlockDevice::Result result = hal::BlockDevice::Result::success)
        {
            device.WriteBlocks(infra::MakeRange(block), number, [this](hal::BlockDevice::Result result)
                {
                    done.callback(result);
                });
            Expect(result);
        }

        Block Read(uint32_t number)
        {
            Block block;
            block.fill(0xcc);
            device.ReadBlocks(infra::MakeRange(block), number, [this](hal::BlockDevice::Result result)
                {
                    done.callback(result);
                });
            Expect(hal::BlockDevice::Result::success);
            return block;
        }

        void Expect(hal::BlockDevice::Result result)
        {
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        Block Filled(uint8_t value)
        {
            Block block;
            block.fill(value);
            return block;
        }

        fs::SparseBlockDevice::WithStorage<2, 16> device{ 1000 };
        testing::StrictMock<infra::MockCallback<void(hal::BlockDevice::Result)>> done;
    };
}

TEST_F(SparseBlockDeviceTest, reports_its_geometry)
{
    EXPECT_EQ(16u, device.BlockSize());
    EXPECT_EQ(1000u, device.NumberOfBlocks());
}

TEST_F(SparseBlockDeviceTest, absent_block_reads_as_zeros)
{
    EXPECT_EQ(Block{}, Read(999));
}

TEST_F(SparseBlockDeviceTest, written_block_reads_back)
{
    Write(Filled(0x5a), 700);

    EXPECT_EQ(Filled(0x5a), Read(700));
    EXPECT_EQ(1u, device.StoredBlocks());
}

TEST_F(SparseBlockDeviceTest, multi_block_requests)
{
    std::array<uint8_t, 32> data;
    data.fill(0x11);
    data[16] = 0x22;

    device.WriteBlocks(infra::MakeRange(data), 3, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::success);

    std::array<uint8_t, 48> read{};
    device.ReadBlocks(infra::MakeRange(read), 2, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::success);

    EXPECT_EQ(0x00, read[0]);
    EXPECT_EQ(0x11, read[16]);
    EXPECT_EQ(0x22, read[32]);
}

TEST_F(SparseBlockDeviceTest, overwriting_a_block_keeps_one_slot)
{
    Write(Filled(1), 5);
    Write(Filled(2), 5);

    EXPECT_EQ(Filled(2), Read(5));
    EXPECT_EQ(1u, device.StoredBlocks());
}

TEST_F(SparseBlockDeviceTest, zero_write_frees_its_slot)
{
    Write(Filled(1), 5);
    Write(Block{}, 5);
    Write(Block{}, 6);

    EXPECT_EQ(Block{}, Read(5));
    EXPECT_EQ(0u, device.StoredBlocks());
}

TEST_F(SparseBlockDeviceTest, freed_slot_is_reused)
{
    Write(Filled(1), 5);
    Write(Filled(2), 6);
    Write(Block{}, 5);
    Write(Filled(3), 7);

    EXPECT_EQ(Filled(3), Read(7));
    EXPECT_EQ(Filled(2), Read(6));
}

TEST_F(SparseBlockDeviceTest, discard_frees_the_blocks_in_its_range)
{
    Write(Filled(1), 5);
    Write(Filled(2), 9);

    device.EraseBlocks(5, 9, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::success);

    EXPECT_EQ(Block{}, Read(5));
    EXPECT_EQ(Filled(2), Read(9));
}

TEST_F(SparseBlockDeviceTest, completes_through_the_event_dispatcher)
{
    Block block{};
    device.ReadBlocks(infra::MakeRange(block), 0, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });

    testing::Mock::VerifyAndClearExpectations(&done);
    Expect(hal::BlockDevice::Result::success);
}

TEST_F(SparseBlockDeviceTest, out_of_range_requests_fail)
{
    Block block{};
    device.ReadBlocks(infra::MakeRange(block), 1000, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::outOfRange);

    Write(Filled(1), 1000, hal::BlockDevice::Result::outOfRange);

    device.EraseBlocks(9, 8, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::outOfRange);

    device.EraseBlocks(0, 1001, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::outOfRange);
}

TEST_F(SparseBlockDeviceTest, injected_failure_fails_the_next_request_only)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);
    Write(Filled(1), 5, hal::BlockDevice::Result::crcError);

    EXPECT_EQ(Block{}, Read(5));
}

TEST_F(SparseBlockDeviceTest, flush_succeeds_unless_a_failure_is_injected)
{
    device.Flush([this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::success);

    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);
    device.Flush([this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::timeout);
}

TEST_F(SparseBlockDeviceTest, synchronous_access_for_arranging_tests)
{
    device.WriteBlock(42, infra::MakeRange(Filled(7)));

    Block block;
    device.ReadBlock(42, infra::MakeRange(block));

    EXPECT_EQ(Filled(7), block);
    EXPECT_EQ(Filled(7), Read(42));
}

TEST_F(SparseBlockDeviceTest, counts_requests)
{
    Write(Filled(1), 5);
    Read(5);
    Read(6);
    device.EraseBlocks(0, 1, [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::success);
    device.Flush([this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        });
    Expect(hal::BlockDevice::Result::success);

    EXPECT_EQ(1u, device.WriteRequests());
    EXPECT_EQ(2u, device.ReadRequests());
    EXPECT_EQ(1u, device.EraseRequests());
    EXPECT_EQ(1u, device.FlushRequests());
}
