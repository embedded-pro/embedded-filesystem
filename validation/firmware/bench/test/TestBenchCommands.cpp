#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/bench/BenchCommands.hpp"
#include "validation/firmware/bench/test_doubles/TimedFlash.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

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
        std::array<validation::DeviceEntry, 2> devices{ {
            { { "ram", validation::DeviceKind::nor, 8, 256, 64, "", 0, "" }, true },
            { { "absent", validation::DeviceKind::nor, 8, 256, 64, "", 0, "" }, false },
        } };
        const std::array<validation::NorBenchTarget, 2> targets{ { { devices[0], flash }, { devices[1], flash } } };
        validation::ResultLines results{ response };
        validation::NorBench bench{ results, validation::FakeCycleCounter::clockHz };
        std::optional<validation::BenchCommands> commands{ std::in_place, context, bench, infra::MakeRange(targets) };

        void Construct(infra::MemoryRange<const validation::NorBenchTarget> range)
        {
            commands.reset();
            commands.emplace(context, bench, range);
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
