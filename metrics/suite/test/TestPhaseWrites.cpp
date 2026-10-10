#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/suite/PhaseWrites.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <set>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    const metrics::BlockDeviceModel testCard{ "test-card", 512, 64, 16, 2, { 100us, 25us, 50us, 0us, 10ms }, { 100us, 25us, 50us, 250ms, 10ms } };

    class PhaseWritesTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        void Write(uint32_t firstBlock, uint32_t blocks)
        {
            std::vector<uint8_t> data(blocks * 512);
            metered.WriteBlocks(infra::MakeRange(data), firstBlock, [](hal::BlockDevice::Result) {});
            ForwardTime(1s);
        }

        std::vector<uint8_t> storage = std::vector<uint8_t>(64 * 512);
        hal::BlockDeviceStub stub{ infra::MakeRange(storage), 512 };
        metrics::MeteredBlockDevice metered{ stub, testCard };
        metrics::PhaseWrites phaseWrites{ metered };
    };
}

TEST_F(PhaseWritesTest, a_phase_counts_the_writes_since_the_previous_phase_ended)
{
    Write(0, 4);
    phaseWrites.PhaseEnded("before");
    Write(16, 2);
    Write(18, 2);

    phaseWrites.PhaseEnded("measured");

    const auto& writes = phaseWrites.Of("measured");
    EXPECT_EQ(2, writes.writeRequests);
    EXPECT_EQ(4, writes.blocksWritten);
    EXPECT_EQ(2, writes.alignedWrites);
    EXPECT_EQ((std::set<uint32_t>{ 1 }), writes.allocationUnits);
}

TEST_F(PhaseWritesTest, a_write_spanning_allocation_units_touches_each_of_them)
{
    Write(15, 2);

    phaseWrites.PhaseEnded("measured");

    EXPECT_EQ((std::set<uint32_t>{ 0, 1 }), phaseWrites.Of("measured").allocationUnits);
}

TEST_F(PhaseWritesTest, a_phase_that_recurs_accumulates)
{
    Write(0, 1);
    phaseWrites.PhaseEnded("fill");
    Write(40, 1);
    phaseWrites.PhaseEnded("other");
    Write(48, 1);
    phaseWrites.PhaseEnded("fill");

    const auto& writes = phaseWrites.Of("fill");
    EXPECT_EQ(2, writes.writeRequests);
    EXPECT_EQ((std::set<uint32_t>{ 0, 3 }), writes.allocationUnits);
}

TEST_F(PhaseWritesTest, an_unknown_phase_has_no_writes)
{
    EXPECT_EQ(0, phaseWrites.Of("unknown").writeRequests);
}

TEST(WritesOfPhaseTest, derives_the_device_metrics)
{
    metrics::WritesOfPhase writes{ 4, 32, 3, { 1, 2 } };

    EXPECT_DOUBLE_EQ(8.0, writes.MeanBlocksPerWrite());
    EXPECT_DOUBLE_EQ(0.75, writes.AlignedShare());
    EXPECT_DOUBLE_EQ(4.0, writes.AllocationUnitsPerMebibyte(512 * 1024));
}

TEST(WritesOfPhaseTest, without_writes_the_metrics_are_zero)
{
    metrics::WritesOfPhase writes;

    EXPECT_DOUBLE_EQ(0.0, writes.MeanBlocksPerWrite());
    EXPECT_DOUBLE_EQ(0.0, writes.AlignedShare());
    EXPECT_DOUBLE_EQ(0.0, writes.AllocationUnitsPerMebibyte(0));
}
