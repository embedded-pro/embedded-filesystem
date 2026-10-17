#include "infra/util/test_helper/MockCallback.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/bench/NorBench.hpp"
#include "validation/firmware/bench/test_doubles/TimedFlash.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>

using namespace std::chrono_literals;

namespace
{
    class NorBenchTest
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

        bool Erased() const
        {
            return std::all_of(storage.begin(), storage.end(), [](uint8_t byte)
                {
                    return byte == 0xff;
                });
        }

        std::array<uint8_t, 8 * 256> storage{};
        validation::TimedFlash timed{ infra::MakeRange(storage), 256 };
        validation::FakeCycleCounter cycles;
        validation::CountingFlash flash{ timed, 64, cycles, validation::FakeCycleCounter::clockHz, { 1s, 1s, 1s } };
        validation::ResultLines results{ response };
        validation::NorBench bench{ results, validation::FakeCycleCounter::clockHz };
        testing::StrictMock<infra::MockCallback<void(validation::BenchOutcome)>> done;
    };
}

TEST_F(NorBenchTest, programs_at_erased_page_aligned_addresses_and_reports_the_durations_of_the_programs)
{
    bench.Program(flash, "ram", 16, 6, Done());
    EXPECT_TRUE(bench.Running());
    Await();

    EXPECT_EQ("E0,1 P0,16 P64,16 P128,16 P192,16 E1,2 P256,16 P320,16 E0,2 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=ram op=program size=16 n=6 minNs=100000 medianNs=100000 p99Ns=100000 maxNs=100000 meanNs=100000\r\n", Output());
}

TEST_F(NorBenchTest, leaves_the_sectors_it_programmed_erased)
{
    bench.Program(flash, "ram", 64, 5, Done());
    Await();

    EXPECT_TRUE(Erased());
}

TEST_F(NorBenchTest, a_program_of_more_than_a_page_starts_on_the_next_free_page)
{
    bench.Program(flash, "ram", 100, 3, Done());
    Await();

    EXPECT_EQ("E0,1 P0,100 P128,100 E1,2 P256,100 E0,2 ", Log());
}

TEST_F(NorBenchTest, erases_sectors_filled_with_data_and_reports_the_durations_of_those_erases)
{
    timed.eraseDelay = 3ms;
    bench.Erase(flash, "ram", 2, Done());
    Await();

    EXPECT_EQ("E0,1 P0,256 E0,1 E1,2 P256,256 E1,2 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=ram op=erase size=256 n=2 minNs=3000000 medianNs=3000000 p99Ns=3000000 maxNs=3000000 meanNs=3000000\r\n", Output());
    EXPECT_TRUE(Erased());
}

TEST_F(NorBenchTest, reads_at_consecutive_addresses)
{
    bench.Read(flash, "ram", 100, 3, Done());
    Await();

    EXPECT_EQ("R0,100 R100,100 R200,100 ", Log());
    EXPECT_EQ("\r\nEVT bench id=1 device=ram op=read size=100 n=3 minNs=10000 medianNs=10000 p99Ns=10000 maxNs=10000 meanNs=10000\r\n", Output());
}

TEST_F(NorBenchTest, reads_wrap_to_the_start_of_the_device)
{
    bench.Read(flash, "ram", 1000, 3, Done());
    Await();

    EXPECT_EQ("R0,1000 R1000,1000 R0,1000 ", Log());
}

TEST_F(NorBenchTest, reports_the_minimum_median_99th_percentile_maximum_and_mean)
{
    for (auto delay : { 10us, 30us, 20us, 40us })
        timed.readDelays.push_back(delay);

    bench.Read(flash, "ram", 16, 4, Done());
    Await();

    EXPECT_THAT(Output(), testing::HasSubstr(" minNs=10000 medianNs=20000 p99Ns=40000 maxNs=40000 meanNs=25000\r\n"));
}

TEST_F(NorBenchTest, a_request_that_outlasts_its_guard_ends_the_benchmark_once_it_completes)
{
    validation::CountingFlash guarded{ timed, 64, cycles, validation::FakeCycleCounter::clockHz, { 50us, 1s, 1s } };
    timed.readDelay = 100us;
    bench.Read(guarded, "ram", 16, 3, Done());

    EXPECT_CALL(done, callback(validation::BenchOutcome::timeout));
    ForwardTime(50us);
    EXPECT_TRUE(bench.Running());

    ForwardTime(1s);
    EXPECT_FALSE(bench.Running());
    EXPECT_EQ("R0,16 ", Log());
    EXPECT_EQ("", Output());
}

TEST_F(NorBenchTest, tells_whether_a_benchmark_fits_on_the_device)
{
    EXPECT_TRUE(validation::NorBench::ProgramFits(flash, 16, 32));
    EXPECT_FALSE(validation::NorBench::ProgramFits(flash, 16, 33));
    EXPECT_TRUE(validation::NorBench::ProgramFits(flash, 256, 8));
    EXPECT_FALSE(validation::NorBench::ProgramFits(flash, 257, 1));
    EXPECT_FALSE(validation::NorBench::ProgramFits(flash, 0, 1));

    EXPECT_TRUE(validation::NorBench::EraseFits(flash, 8));
    EXPECT_FALSE(validation::NorBench::EraseFits(flash, 9));

    EXPECT_TRUE(validation::NorBench::ReadFits(flash, 2048));
    EXPECT_FALSE(validation::NorBench::ReadFits(flash, 2049));
    EXPECT_FALSE(validation::NorBench::ReadFits(flash, 0));
}
