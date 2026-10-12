#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "metrics/power_cut/PowerCutBlockDevice.hpp"
#include "gmock/gmock.h"
#include <vector>

namespace
{
    class PowerCutBlockDeviceTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Write(uint32_t firstBlock, uint32_t blocks, uint8_t value)
        {
            std::vector<uint8_t> data(blocks * 16, value);
            cut.WriteBlocks(infra::MakeRange(data), firstBlock, [this](hal::BlockDevice::Result result)
                {
                    lastResult = result;
                    ++completions;
                });
            ExecuteAllActions();
        }

        void Flush()
        {
            cut.Flush([this](hal::BlockDevice::Result)
                {
                    ++completions;
                });
            ExecuteAllActions();
        }

        std::vector<uint8_t> BlockContent(uint32_t block) const
        {
            return std::vector<uint8_t>(storage.begin() + block * 16, storage.begin() + (block + 1) * 16);
        }

        std::vector<uint8_t> storage = std::vector<uint8_t>(8 * 16, 0);
        hal::BlockDeviceStub stub{ infra::MakeRange(storage), 16 };
        metrics::PowerCutBlockDevice cut{ stub, infra::MakeRange(storage) };
        int completions{ 0 };
        hal::BlockDevice::Result lastResult{ hal::BlockDevice::Result::failed };
    };
}

TEST_F(PowerCutBlockDeviceTest, forwards_requests_while_not_armed)
{
    std::vector<uint8_t> data(16);

    Write(1, 1, 0x11);
    cut.ReadBlocks(infra::MakeRange(data), 1, [this](hal::BlockDevice::Result)
        {
            ++completions;
        });
    ExecuteAllActions();

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), data);
    EXPECT_EQ(2, completions);
    EXPECT_EQ(16, cut.BlockSize());
    EXPECT_EQ(8, cut.NumberOfBlocks());
}

TEST_F(PowerCutBlockDeviceTest, counts_every_written_block_as_a_write_step)
{
    Write(0, 3, 0x11);

    EXPECT_EQ(3, cut.WriteSteps());
}

TEST_F(PowerCutBlockDeviceTest, counts_a_discard_as_one_write_step)
{
    cut.EraseBlocks(0, 4, [](hal::BlockDevice::Result) {});
    ExecuteAllActions();

    EXPECT_EQ(1, cut.WriteSteps());
}

TEST_F(PowerCutBlockDeviceTest, does_not_count_reads_and_flushes_as_write_steps)
{
    std::vector<uint8_t> data(16);

    cut.ReadBlocks(infra::MakeRange(data), 0, [](hal::BlockDevice::Result) {});
    ExecuteAllActions();
    Flush();

    EXPECT_EQ(0, cut.WriteSteps());
}

TEST_F(PowerCutBlockDeviceTest, write_interrupted_at_its_third_block_writes_the_first_two_blocks)
{
    cut.Arm({ 2, metrics::InterruptedBlock::unwritten });

    Write(0, 4, 0x11);

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), BlockContent(0));
    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), BlockContent(1));
    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(2));
    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(3));
    EXPECT_TRUE(cut.PoweredDown());
}

TEST_F(PowerCutBlockDeviceTest, interrupted_block_is_written_in_written_mode)
{
    cut.Arm({ 1, metrics::InterruptedBlock::written });

    Write(0, 3, 0x11);

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), BlockContent(1));
    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(2));
}

TEST_F(PowerCutBlockDeviceTest, interrupted_block_is_torn_in_half_by_default)
{
    cut.Arm({ 0, metrics::InterruptedBlock::torn });

    Write(0, 1, 0x11);

    std::vector<uint8_t> expected(8, 0x11);
    expected.resize(16, metrics::PowerCutBlockDevice::tornPattern);
    EXPECT_EQ(expected, BlockContent(0));
}

TEST_F(PowerCutBlockDeviceTest, interrupted_block_is_torn_after_the_given_number_of_bytes)
{
    cut.Arm({ 0, metrics::InterruptedBlock::torn, false, 4 });

    Write(0, 1, 0x11);

    std::vector<uint8_t> expected(4, 0x11);
    expected.resize(16, metrics::PowerCutBlockDevice::tornPattern);
    EXPECT_EQ(expected, BlockContent(0));
}

TEST_F(PowerCutBlockDeviceTest, interrupted_discard_is_not_applied_when_unwritten)
{
    Write(0, 2, 0x11);
    cut.Arm({ 2, metrics::InterruptedBlock::unwritten });

    cut.EraseBlocks(0, 2, [](hal::BlockDevice::Result) {});
    ExecuteAllActions();

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), BlockContent(0));
}

TEST_F(PowerCutBlockDeviceTest, interrupted_discard_is_applied_when_written)
{
    Write(0, 2, 0x11);
    cut.Arm({ 2, metrics::InterruptedBlock::written });

    cut.EraseBlocks(0, 2, [](hal::BlockDevice::Result) {});
    ExecuteAllActions();

    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(0));
    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(1));
}

TEST_F(PowerCutBlockDeviceTest, interrupted_request_is_never_completed)
{
    cut.Arm({ 0, metrics::InterruptedBlock::written });

    Write(0, 1, 0x11);

    EXPECT_EQ(0, completions);
}

TEST_F(PowerCutBlockDeviceTest, requests_after_the_cut_have_no_effect_and_are_never_completed)
{
    cut.Arm({ 0, metrics::InterruptedBlock::unwritten });
    Write(0, 1, 0x11);

    Write(1, 1, 0x22);
    Flush();

    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(1));
    EXPECT_EQ(0, completions);
}

TEST_F(PowerCutBlockDeviceTest, unflushed_writes_are_kept_when_loss_is_disabled)
{
    Write(0, 1, 0x11);
    cut.Arm({ 1, metrics::InterruptedBlock::unwritten });

    Write(1, 1, 0x22);

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), BlockContent(0));
}

TEST_F(PowerCutBlockDeviceTest, unflushed_writes_are_reverted_on_the_cut_when_loss_is_enabled)
{
    Write(0, 1, 0x11);
    cut.Arm({ 1, metrics::InterruptedBlock::written, true });

    Write(1, 1, 0x22);

    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(0));
    EXPECT_EQ(std::vector<uint8_t>(16, 0), BlockContent(1));
}

TEST_F(PowerCutBlockDeviceTest, flushed_writes_survive_the_cut_when_loss_is_enabled)
{
    Write(0, 1, 0x11);
    Flush();
    cut.Arm({ 1, metrics::InterruptedBlock::unwritten, true });

    Write(1, 1, 0x22);

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), BlockContent(0));
}

TEST_F(PowerCutBlockDeviceTest, a_block_written_twice_since_the_flush_reverts_to_its_content_at_the_flush)
{
    Write(0, 1, 0x11);
    Flush();
    Write(0, 1, 0x22);
    Write(0, 1, 0x33);
    cut.Arm({ 3, metrics::InterruptedBlock::unwritten, true });

    Write(1, 1, 0x44);

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), BlockContent(0));
}

TEST_F(PowerCutBlockDeviceTest, reads_see_unflushed_writes_before_the_cut)
{
    std::vector<uint8_t> data(16);
    Write(0, 1, 0x11);

    cut.ReadBlocks(infra::MakeRange(data), 0, [](hal::BlockDevice::Result) {});
    ExecuteAllActions();

    EXPECT_EQ(std::vector<uint8_t>(16, 0x11), data);
}

TEST_F(PowerCutBlockDeviceTest, an_error_of_the_wrapped_device_is_forwarded)
{
    stub.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    Write(0, 1, 0x11);

    EXPECT_EQ(hal::BlockDevice::Result::timeout, lastResult);
}
