#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/bench/BenchCommands.hpp"
#include "validation/firmware/bench/test_doubles/TimedBlockDevice.hpp"
#include "validation/firmware/bench/test_doubles/TimedFlash.hpp"
#include "validation/firmware/devices/BlockDeviceRegion.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

using namespace std::chrono_literals;

namespace
{
    class BenchCommandsTest
        : public services::HilFixture
    {
    public:
        std::array<uint8_t, 8 * 256> storage{};
        validation::TimedFlash timed{ infra::MakeRange(storage), 256 };
        validation::FakeCycleCounter cycles;
        validation::CountingFlash flash{ timed, 64, cycles, validation::FakeCycleCounter::clockHz, { 1s, 1s, 1s } };
        std::array<uint8_t, 64 * 512> blockStorage{};
        validation::TimedBlockDevice timedBlocks{ infra::MakeRange(blockStorage) };
        validation::CountingBlockDevice blocks{ timedBlocks, 8, cycles, validation::FakeCycleCounter::clockHz, 1s };
        std::array<validation::DeviceEntry, 4> devices{ {
            { { "ram", validation::DeviceKind::nor, 8, 256, 64, "", 0, "" }, true },
            { { "absent", validation::DeviceKind::nor, 8, 256, 64, "", 0, "" }, false },
            { { "card", validation::DeviceKind::block, 64, 512, 0, "", 0, "" }, true },
            { { "nocard", validation::DeviceKind::block, 64, 512, 0, "", 0, "" }, false },
        } };
        const std::array<validation::NorBenchTarget, 2> targets{ { { devices[0], flash }, { devices[1], flash } } };
        const std::array<validation::BlockBenchTarget, 2> blockTargets{ { { devices[2], blocks }, { devices[3], blocks } } };
        validation::ResultLines results{ response };
        validation::NorBench bench{ results, validation::FakeCycleCounter::clockHz };
        validation::BlockBench blockBench{ results, validation::FakeCycleCounter::clockHz };
        std::optional<validation::BenchCommands> commands{ std::in_place, context, bench, blockBench, infra::MakeRange(targets), infra::MakeRange(blockTargets) };

        void Construct(infra::MemoryRange<const validation::NorBenchTarget> range)
        {
            commands.reset();
            commands.emplace(context, bench, blockBench, range, infra::MakeRange(blockTargets));
        }
    };
}

TEST_F(BenchCommandsTest, program_answers_ok_once_its_record_is_printed)
{
    Execute("bench.program ram size=16 n=2");
    ForwardTime(10s);

    EXPECT_EQ("\r\nEVT bench id=1 device=ram op=program size=16 n=2 minNs=100000 medianNs=100000 p99Ns=100000 maxNs=100000 meanNs=100000\r\n\r\nOK\r\n", Output());
}

TEST_F(BenchCommandsTest, erase_benchmarks_the_erase_of_sectors)
{
    Execute("bench.erase ram n=1");
    ForwardTime(10s);

    const auto output = Output();
    EXPECT_THAT(output, testing::StartsWith("\r\nEVT bench id=1 device=ram op=erase size=256 n=1 "));
    EXPECT_THAT(output, testing::EndsWith("OK\r\n"));
}

TEST_F(BenchCommandsTest, read_benchmarks_reads_of_a_size)
{
    Execute("bench.read ram size=128 n=3");
    ForwardTime(10s);

    const auto output = Output();
    EXPECT_THAT(output, testing::StartsWith("\r\nEVT bench id=1 device=ram op=read size=128 n=3 "));
    EXPECT_THAT(output, testing::EndsWith("OK\r\n"));
}

TEST_F(BenchCommandsTest, takes_one_device_and_the_keys_of_its_benchmark)
{
    for (const char* command : { "bench.program ram n=2", "bench.program ram size=16", "bench.program size=16 n=2", "bench.program ram other size=16 n=2",
             "bench.erase ram", "bench.erase ram n=1 size=4", "bench.read ram n=1", "bench.read ram size=4 n=1 x=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR usage\r\n", Output()) << command;
    }
}

TEST_F(BenchCommandsTest, counts_and_sizes_out_of_range_are_rejected)
{
    for (const char* command : { "bench.program ram size=16 n=0", "bench.program ram size=16 n=1025", "bench.program ram size=16 n=33", "bench.program ram size=257 n=1",
             "bench.erase ram n=9", "bench.erase ram n=0", "bench.read ram size=2049 n=1", "bench.read ram size=0 n=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR range\r\n", Output()) << command;
    }
}

TEST_F(BenchCommandsTest, a_device_that_is_absent_or_unknown_is_unsupported)
{
    Execute("bench.read absent size=16 n=1");
    EXPECT_EQ("ERR unsupported\r\n", Output());

    Execute("bench.read qspi size=16 n=1");
    EXPECT_EQ("ERR unsupported\r\n", Output());
}

TEST_F(BenchCommandsTest, a_second_benchmark_while_one_runs_is_busy)
{
    Execute("bench.read ram size=16 n=1");
    Execute("bench.read ram size=16 n=1");

    EXPECT_EQ("ERR busy\r\n", Output());
    ForwardTime(10s);
}

TEST_F(BenchCommandsTest, a_request_that_outlasts_its_guard_answers_timeout_and_keeps_the_device_busy_until_it_completes)
{
    validation::CountingFlash guarded{ timed, 64, cycles, validation::FakeCycleCounter::clockHz, { 50us, 1s, 1s } };
    const std::array<validation::NorBenchTarget, 1> slow{ { { devices[0], guarded } } };
    Construct(infra::MakeRange(slow));
    timed.readDelay = 100us;

    Execute("bench.read ram size=16 n=2");
    ForwardTime(50us);
    EXPECT_EQ("\r\nERR timeout\r\n", Output());

    Execute("bench.read ram size=16 n=2");
    EXPECT_EQ("ERR busy\r\n", Output());

    ForwardTime(1s);
    timed.readDelay = 10us;
    Execute("bench.read ram size=16 n=1");
    ForwardTime(1s);
    EXPECT_THAT(Output(), testing::EndsWith("OK\r\n"));
}

TEST_F(BenchCommandsTest, a_benchmark_that_outlasts_the_timeout_answers_timeout_once)
{
    timed.eraseDelay = validation::BenchCommands::timeout;
    validation::CountingFlash patient{ timed, 64, cycles, validation::FakeCycleCounter::clockHz, { 1s, 1s, 2 * validation::BenchCommands::timeout } };
    const std::array<validation::NorBenchTarget, 1> slow{ { { devices[0], patient } } };
    Construct(infra::MakeRange(slow));

    Execute("bench.erase ram n=1");
    ForwardTime(validation::BenchCommands::timeout);
    EXPECT_EQ("\r\nERR timeout\r\n", Output());

    ForwardTime(3 * validation::BenchCommands::timeout);
    const auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr("EVT bench id=1 device=ram op=erase"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("OK")));
}

TEST_F(BenchCommandsTest, check_answers_ok_on_a_device_whose_tags_read_back)
{
    Execute("dev.check ram");
    ForwardTime(10s);

    EXPECT_EQ("\r\nOK\r\n", Output());
}

TEST_F(BenchCommandsTest, check_answers_failed_on_a_device_with_a_missing_address_line)
{
    timed.addressMask = 1023;

    Execute("dev.check ram");
    ForwardTime(10s);

    EXPECT_EQ("\r\nERR failed\r\n", Output());
}

TEST_F(BenchCommandsTest, a_device_that_a_workload_uses_is_busy)
{
    flash.OnTimeout([] {});

    for (const char* command : { "bench.read ram size=16 n=1", "dev.check ram" })
    {
        Execute(command);
        EXPECT_EQ("ERR busy\r\n", Output()) << command;
    }

    flash.OnTimeout(nullptr);
}

TEST_F(BenchCommandsTest, check_takes_one_present_device_and_waits_for_a_running_benchmark)
{
    Execute("dev.check");
    EXPECT_EQ("ERR usage\r\n", Output());

    Execute("dev.check absent");
    EXPECT_EQ("ERR unsupported\r\n", Output());

    Execute("bench.read ram size=16 n=1");
    Execute("dev.check ram");
    EXPECT_EQ("ERR busy\r\n", Output());
    ForwardTime(10s);
}

TEST_F(BenchCommandsTest, blocks_benchmarks_reads_and_writes_of_consecutive_blocks)
{
    Execute("bench.blocks card read blocks=4 n=2");
    ForwardTime(10s);
    EXPECT_EQ("\r\nEVT bench id=1 device=card op=read size=2048 n=2 minNs=100000 medianNs=100000 p99Ns=100000 maxNs=100000 meanNs=100000\r\n\r\nOK\r\n", Output());

    Execute("bench.blocks card write blocks=1 n=1");
    ForwardTime(10s);
    EXPECT_THAT(Output(), testing::StartsWith("\r\nEVT bench id=2 device=card op=write size=512 n=1 "));
}

TEST_F(BenchCommandsTest, units_flush_and_discard_benchmark_their_requests)
{
    const std::array<std::pair<const char*, const char*>, 3> cases{ {
        { "bench.units card units=2 n=3", " op=units size=512 units=2 n=6 " },
        { "bench.flush card n=2", " op=flush size=512 n=2 " },
        { "bench.discard card blocks=8 n=2", " op=discard size=4096 n=2 " },
    } };

    for (const auto& [command, record] : cases)
    {
        Execute(command);
        ForwardTime(10s);
        const auto output = Output();
        EXPECT_THAT(output, testing::HasSubstr(record)) << command;
        EXPECT_THAT(output, testing::EndsWith("OK\r\n")) << command;
    }
}

TEST_F(BenchCommandsTest, block_benchmarks_take_one_device_and_their_keys)
{
    for (const char* command : { "bench.blocks card blocks=4 n=1", "bench.blocks card erase blocks=4 n=1", "bench.blocks card read n=1", "bench.blocks card read blocks=4",
             "bench.units card n=1", "bench.units card units=2", "bench.flush card", "bench.flush card n=1 blocks=2", "bench.discard card n=1", "bench.discard card blocks=8 n=1 units=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR usage\r\n", Output()) << command;
    }
}

TEST_F(BenchCommandsTest, block_counts_out_of_range_are_rejected)
{
    for (const char* command : { "bench.blocks card read blocks=0 n=1", "bench.blocks card read blocks=129 n=1", "bench.blocks card write blocks=4 n=0", "bench.units card units=9 n=1",
             "bench.units card units=8 n=129", "bench.flush card n=1025", "bench.discard card blocks=65 n=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR range\r\n", Output()) << command;
    }
}

TEST_F(BenchCommandsTest, a_flush_of_a_block_device_without_blocks_is_out_of_range)
{
    validation::BlockDeviceRegion beyond{ timedBlocks, 64, 8 };
    validation::CountingBlockDevice empty{ beyond, 8, cycles, validation::FakeCycleCounter::clockHz, 1s };
    const std::array<validation::BlockBenchTarget, 1> emptyTargets{ { { devices[2], empty } } };
    commands.reset();
    commands.emplace(context, bench, blockBench, infra::MakeRange(targets), infra::MakeRange(emptyTargets));

    Execute("bench.flush card n=1");
    EXPECT_EQ("ERR range\r\n", Output());
}

TEST_F(BenchCommandsTest, a_block_device_that_is_absent_or_of_the_other_kind_is_unsupported)
{
    for (const char* command : { "bench.flush nocard n=1", "bench.flush ram n=1", "bench.read card size=16 n=1" })
    {
        Execute(command);
        EXPECT_EQ("ERR unsupported\r\n", Output()) << command;
    }
}

TEST_F(BenchCommandsTest, a_benchmark_of_either_kind_while_another_runs_is_busy)
{
    Execute("bench.read ram size=16 n=1");
    Execute("bench.flush card n=1");
    EXPECT_EQ("ERR busy\r\n", Output());
    ForwardTime(10s);
    Output();

    Execute("bench.flush card n=1");
    Execute("bench.read ram size=16 n=1");
    EXPECT_EQ("ERR busy\r\n", Output());
    ForwardTime(10s);
}

TEST_F(BenchCommandsTest, a_block_device_that_a_workload_uses_is_busy)
{
    blocks.OnTimeout([] {});

    Execute("bench.flush card n=1");
    EXPECT_EQ("ERR busy\r\n", Output());

    blocks.OnTimeout(nullptr);
}

TEST_F(BenchCommandsTest, a_failed_block_request_answers_failed)
{
    timedBlocks.failRequest = 0;

    Execute("bench.blocks card read blocks=1 n=1");
    ForwardTime(10s);
    EXPECT_EQ("\r\nERR failed\r\n", Output());
}
