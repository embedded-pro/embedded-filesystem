#include "infra/util/test_helper/MockCallback.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/bench/BlockBench.hpp"
#include "validation/firmware/bench/test_doubles/TimedBlockDevice.hpp"
#include "validation/firmware/devices/CountingBlockDevice.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <string>

using namespace std::chrono_literals;

namespace
{
    class BlockBenchTest
        : public services::HilFixture
    {
    public:
        infra::Function<void(validation::BenchOutcome)> Done()
        {
            return [this](validation::BenchOutcome outcome)
            {
                done.callback(outcome);
            };
        }

        void Await(validation::BenchOutcome outcome = validation::BenchOutcome::done)
        {
            EXPECT_CALL(done, callback(outcome));
            ForwardTime(10s);
            EXPECT_FALSE(bench.Running());
        }

        std::string Log() const
        {
            return std::string{ timed.log.data(), timed.log.size() };
        }

        std::array<uint8_t, 64 * 512> storage{};
        validation::TimedBlockDevice timed{ infra::MakeRange(storage) };
        validation::FakeCycleCounter cycles;
        validation::CountingBlockDevice device{ timed, 8, cycles, validation::FakeCycleCounter::clockHz, 1s };
        validation::ResultLines results{ response };
        validation::BlockBench bench{ results, validation::FakeCycleCounter::clockHz };
        testing::StrictMock<infra::MockCallback<void(validation::BenchOutcome)>> done;
    };
}

TEST_F(BlockBenchTest, reads_requests_of_consecutive_blocks_and_reports_their_durations)
{
    bench.Read(device, "card", 4, 3, Done());
    EXPECT_TRUE(bench.Running());
    Await();

    EXPECT_EQ("R0,4 R4,4 R8,4 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=card op=read size=2048 n=3 minNs=100000 medianNs=100000 p99Ns=100000 maxNs=100000 meanNs=100000\r\n", Output());
}

TEST_F(BlockBenchTest, requests_start_again_from_the_first_block_where_the_next_does_not_fit)
{
    bench.Read(device, "card", 24, 3, Done());
    Await();

    EXPECT_EQ("R0,24 R24,24 R0,24 ", Log());
}

TEST_F(BlockBenchTest, writes_requests_of_consecutive_blocks)
{
    bench.Write(device, "card", 2, 2, Done());
    Await();

    EXPECT_EQ("W0,2 W2,2 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=card op=write size=1024 n=2 minNs=200000 medianNs=200000 p99Ns=200000 maxNs=200000 meanNs=200000\r\n", Output());
}

TEST_F(BlockBenchTest, units_writes_one_block_into_each_allocation_unit_in_turn)
{
    bench.Units(device, "card", 3, 2, Done());
    Await();

    EXPECT_EQ("W0,1 W8,1 W16,1 W1,1 W9,1 W17,1 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=card op=units size=512 units=3 n=6 minNs=200000 medianNs=200000 p99Ns=200000 maxNs=200000 meanNs=200000\r\n", Output());
}

TEST_F(BlockBenchTest, flush_writes_a_block_before_each_flush_and_measures_the_flush)
{
    bench.Flush(device, "card", 2, Done());
    Await();

    EXPECT_EQ("W0,1 F0,0 W1,1 F0,0 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=card op=flush size=512 n=2 minNs=1000000 medianNs=1000000 p99Ns=1000000 maxNs=1000000 meanNs=1000000\r\n", Output());
}

TEST_F(BlockBenchTest, discard_writes_the_first_block_of_each_range_and_measures_the_discard)
{
    bench.Discard(device, "card", 8, 2, Done());
    Await();

    EXPECT_EQ("W0,1 D0,8 W8,1 D8,16 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=card op=discard size=4096 n=2 minNs=2000000 medianNs=2000000 p99Ns=2000000 maxNs=2000000 meanNs=2000000\r\n", Output());
}

TEST_F(BlockBenchTest, a_failed_request_ends_the_benchmark_without_a_record)
{
    timed.failRequest = 1;
    bench.Read(device, "card", 1, 3, Done());
    Await(validation::BenchOutcome::failed);

    EXPECT_EQ("R0,1 R1,1 ", Log());
    EXPECT_EQ("", Output());
}

TEST_F(BlockBenchTest, a_request_past_its_guard_ends_the_benchmark_with_a_timeout_and_keeps_it_running_until_the_request_completes)
{
    timed.flushDelay = 3s;
    bench.Flush(device, "card", 1, Done());

    EXPECT_CALL(done, callback(validation::BenchOutcome::timeout));
    ForwardTime(1100ms);
    EXPECT_TRUE(bench.Running());
    EXPECT_TRUE(device.InUse());

    ForwardTime(2s);
    EXPECT_FALSE(bench.Running());
    EXPECT_FALSE(device.InUse());
    EXPECT_EQ("", Output());
}

TEST_F(BlockBenchTest, requests_of_consecutive_blocks_fit_up_to_the_buffer_and_the_device)
{
    EXPECT_TRUE(validation::BlockBench::BlocksFit(device, 1, 1));
    EXPECT_TRUE(validation::BlockBench::BlocksFit(device, 64, 1024));
    EXPECT_FALSE(validation::BlockBench::BlocksFit(device, 0, 1));
    EXPECT_FALSE(validation::BlockBench::BlocksFit(device, 65, 1));
    EXPECT_FALSE(validation::BlockBench::BlocksFit(device, 4, 0));
    EXPECT_FALSE(validation::BlockBench::BlocksFit(device, 4, 1025));
}

TEST_F(BlockBenchTest, units_fit_up_to_eight_units_on_the_device_and_1024_writes)
{
    EXPECT_TRUE(validation::BlockBench::UnitsFit(device, 8, 128));
    EXPECT_FALSE(validation::BlockBench::UnitsFit(device, 0, 1));
    EXPECT_FALSE(validation::BlockBench::UnitsFit(device, 9, 1));
    EXPECT_FALSE(validation::BlockBench::UnitsFit(device, 8, 129));
    EXPECT_FALSE(validation::BlockBench::UnitsFit(device, 2, 0));
}

TEST_F(BlockBenchTest, flushes_and_discards_fit_on_the_device)
{
    EXPECT_TRUE(validation::BlockBench::FlushFits(device, 1024));
    EXPECT_FALSE(validation::BlockBench::FlushFits(device, 0));
    EXPECT_TRUE(validation::BlockBench::DiscardFits(device, 64, 2));
    EXPECT_FALSE(validation::BlockBench::DiscardFits(device, 0, 2));
    EXPECT_FALSE(validation::BlockBench::DiscardFits(device, 65, 2));
    EXPECT_FALSE(validation::BlockBench::DiscardFits(device, 8, 0));
}

TEST_F(BlockBenchTest, units_need_the_units_on_the_device)
{
    validation::TimedBlockDevice small{ infra::Head(infra::MakeRange(storage), 16 * 512) };
    validation::CountingBlockDevice smallDevice{ small, 8, cycles, validation::FakeCycleCounter::clockHz, 1s };

    EXPECT_TRUE(validation::BlockBench::UnitsFit(smallDevice, 2, 1));
    EXPECT_FALSE(validation::BlockBench::UnitsFit(smallDevice, 3, 1));
}
