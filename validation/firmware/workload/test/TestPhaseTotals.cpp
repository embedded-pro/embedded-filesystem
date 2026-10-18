#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/timing/LatencyHistogram.hpp"
#include "validation/firmware/workload/PhaseTotals.hpp"
#include "gmock/gmock.h"
#include <optional>

namespace
{
    const validation::WorkloadContext workloadContext{ "qspi", "embedded-filesystem/nor", "L1", 10, 16, 17764 };
    const validation::WorkloadContext blockContext{ "sd", "embedded-filesystem/fat", "L2", 50, 16, 7748, validation::DeviceKind::block };

    class PhaseTotalsTest
        : public services::HilFixture
    {
    public:
        validation::ResultLines results{ response };
        validation::PhaseTotals totals;
    };
}

TEST_F(PhaseTotalsTest, adds_the_calls_of_an_operation)
{
    totals.Start("L1,4096,64", {}, std::nullopt);

    totals.Add(validation::WorkloadOperation::write, 64, 1'000);
    totals.Add(validation::WorkloadOperation::write, 64, 3'000);

    const auto& write = totals.Of(validation::WorkloadOperation::write);
    EXPECT_EQ("L1,4096,64", totals.Name());
    EXPECT_EQ(2, write.calls);
    EXPECT_EQ(128, write.userBytes);
    EXPECT_EQ(4'000, write.latencyNs);
    EXPECT_EQ(3'000, write.maxNs);
    EXPECT_EQ(1, write.histogram.Count(validation::LatencyHistogram::Bucket(3'000)));
    EXPECT_EQ(0, totals.Of(validation::WorkloadOperation::sync).calls);
}

TEST_F(PhaseTotalsTest, a_new_phase_starts_without_calls)
{
    totals.Start("L1,4096,64", {}, std::nullopt);
    totals.Add(validation::WorkloadOperation::write, 64, 1'000);

    totals.Start("L1,cleanup,4096,64", {}, std::nullopt);

    EXPECT_EQ(0, totals.Of(validation::WorkloadOperation::write).calls);
    EXPECT_EQ(0, totals.Of(validation::WorkloadOperation::write).histogram.Count(validation::LatencyHistogram::Bucket(1'000)));
}

TEST_F(PhaseTotalsTest, prints_the_phase_with_what_the_device_and_the_volume_counted_since_it_started)
{
    validation::DeviceCounts start{ { 1, 100, 1'000, 2, 200, 3, 2'000, 0, 0, 0 }, {} };
    validation::DeviceCounts end{ { 3, 612, 3'000, 2, 200, 3, 2'000, 1, 1, 45'000 }, {} };
    totals.Start("L1,4096,64", start, validation::VolumeCounts{ 1, 2, 3, 4 });

    totals.Print(results, workloadContext, { 5'000, end, validation::VolumeCounts{ 2, 2, 5, 4 } });

    auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr("EVT phase id=1 device=qspi subject=embedded-filesystem/nor workload=L1 phase=L1,4096,64 fill=10 ramBytes=17764 elapsedNs=5000 "));
    EXPECT_THAT(output, testing::HasSubstr("readRequests=2 bytesRead=512 readNs=2000 programRequests=0 bytesProgrammed=0 pagesProgrammed=0 programNs=0 "));
    EXPECT_THAT(output, testing::HasSubstr("eraseRequests=1\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("EVT phase id=1 sectorsErased=1 eraseNs=45000 "));
    EXPECT_THAT(output, testing::HasSubstr("allocatedSectors=1 erasedBlocks=0 checkpoints=2 movedBlocks=0"));
}

TEST_F(PhaseTotalsTest, prints_the_operations_that_were_called_with_their_histogram)
{
    totals.Start("L1,4096,64", {}, std::nullopt);
    totals.Add(validation::WorkloadOperation::write, 64, 1'000);
    totals.Add(validation::WorkloadOperation::write, 64, 3'000);

    totals.Print(results, workloadContext, { 5'000, {}, std::nullopt });

    auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr("write.calls=2 write.userBytes=128 write.latencyNs=4000 write.maxNs=3000 write.histogram=35,1,41,1"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("sync.")));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("allocatedSectors")));
}

TEST_F(PhaseTotalsTest, prints_what_a_block_device_counted_in_the_phase)
{
    validation::DeviceCounts start;
    start.blocks.writeRequests = 1;
    validation::DeviceCounts end;
    end.blocks = { 2, 3, 4'000, 6, 7, 8'000, { 9, 10, 11, 12 }, 13, 14, 15, 16, 17'000, 18, 19, 20'000, 21 };
    end.flash.readRequests = 99;
    totals.Start("L2", start, std::nullopt);

    totals.Print(results, blockContext, { 5'000, end, std::nullopt });

    auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr("readRequests=2 blocksRead=3 readNs=4000 writeRequests=5 blocksWritten=7 writeNs=8000 "));
    EXPECT_THAT(output, testing::HasSubstr("unitsRecent1=9 unitsRecent2=10 unitsRecent3=11"));
    EXPECT_THAT(output, testing::HasSubstr("unitsRecent4=12 unitsNotRecent=13 alignedBlocks=14 "));
    EXPECT_THAT(output, testing::HasSubstr("distinctUnits=15 flushRequests=16 flushNs=17000 discardRequests=18 blocksDiscarded=19 "));
    EXPECT_THAT(output, testing::HasSubstr("discardNs=20000 failedRequests=21"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("bytesRead")));
}
