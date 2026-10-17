#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/cycles/CyclesCommands.hpp"
#include "validation/firmware/cycles/test_doubles/CycleFakes.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>

using namespace std::chrono_literals;

namespace
{
    validation::CyclePlan SmallPlan()
    {
        validation::CyclePlan plan;
        plan.smallFiles = 1;
        plan.bigFileBytes = validation::CycleRun::writeBytes;
        return plan;
    }

    class CyclesCommandsTest
        : public services::HilFixture
    {
    public:
        const validation::DeviceDescription ramFlash{ "ramflash", validation::DeviceKind::nor, 512, 4096, 0, "", 0, "sdram" };
        const validation::DeviceDescription ramBlock{ "ramblock", validation::DeviceKind::block, 4096, 512, 0, "", 0, "sdram" };
        validation::FakeCycleCounter counter;
        validation::FakeIdleCycles idle;
        validation::FakeCycleSubject subject{ counter };
        validation::FakeCycleSubject blockSubject{ counter };
        validation::ResultLines results{ response };
        validation::CycleRun run{ results, counter, idle, SmallPlan() };
        const std::array<validation::CycleTarget, 2> targets{ { { subject, ramFlash }, { blockSubject, ramBlock } } };
        validation::CyclesCommands commands{ context, results, run, infra::MakeRange(targets) };
    };
}

TEST_F(CyclesCommandsTest, runs_a_subject_by_name_and_answers_ok_once_every_operation_has_its_record)
{
    Execute("cycles.run fake n=2");
    ForwardTime(10s);

    const auto output = Output();
    EXPECT_THAT(output, testing::StartsWith("EVT device id=1 device=ramflash kind=nor sectors=512 sectorSize=4096 memory=sdram\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("EVT cycles id=13 subject=fake device=ramflash op=unmount n=2 "));
    EXPECT_THAT(output, testing::EndsWith("\r\nOK\r\n"));
}

TEST_F(CyclesCommandsTest, runs_100_times_without_a_count)
{
    Execute("cycles.run fake");
    ForwardTime(60s);

    EXPECT_THAT(Output(), testing::HasSubstr(" op=format n=100 "));
}

TEST_F(CyclesCommandsTest, describes_a_block_device_with_its_blocks)
{
    blockSubject.name = infra::BoundedConstString("blocks");

    Execute("cycles.run blocks n=1");
    ForwardTime(10s);

    EXPECT_THAT(Output(), testing::StartsWith("EVT device id=1 device=ramblock kind=block blocks=4096 blockSize=512 memory=sdram\r\n"));
}

TEST_F(CyclesCommandsTest, a_failed_run_answers_failed)
{
    subject.failAtCall = 1;

    Execute("cycles.run fake n=1");
    ForwardTime(10s);

    const auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr("EVT cycles.failed id=2 subject=fake op=format run=0 status=-28\r\n"));
    EXPECT_THAT(output, testing::EndsWith("ERR failed\r\n"));
}

TEST_F(CyclesCommandsTest, a_run_that_outlasts_the_timeout_answers_timeout_once)
{
    subject.busyPollsAfterEachCall = 0xffffffff;

    Execute("cycles.run fake n=1");
    ForwardTime(validation::CyclesCommands::timeout + 1s);

    EXPECT_THAT(Output(), testing::EndsWith("ERR timeout\r\n"));
}

TEST_F(CyclesCommandsTest, a_run_that_ends_after_the_timeout_answers_nothing_more)
{
    subject.busyPollsAfterEachCall = 610000;
    subject.failAtCall = 1;

    Execute("cycles.run fake n=1");
    ForwardTime(validation::CyclesCommands::timeout + 20s);

    const auto output = Output();
    EXPECT_THAT(output, testing::HasSubstr("EVT cycles.failed"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("ERR failed")));
    EXPECT_FALSE(run.Running());
}

TEST_F(CyclesCommandsTest, a_second_run_while_one_runs_is_busy)
{
    Execute("cycles.run fake n=1");
    Execute("cycles.run fake n=1");

    EXPECT_THAT(Output(), testing::EndsWith("ERR busy\r\n"));
    ForwardTime(10s);
}

TEST_F(CyclesCommandsTest, an_unknown_subject_is_unsupported)
{
    Execute("cycles.run other");

    EXPECT_EQ("ERR unsupported\r\n", Output());
}

TEST_F(CyclesCommandsTest, the_count_runs_from_1_to_the_capacity_of_the_samples)
{
    Execute("cycles.run fake n=0");
    EXPECT_EQ("ERR range\r\n", Output());

    Execute("cycles.run fake n=201");
    EXPECT_EQ("ERR range\r\n", Output());
}

TEST_F(CyclesCommandsTest, takes_one_subject_and_a_count)
{
    Execute("cycles.run");
    EXPECT_EQ("ERR usage\r\n", Output());

    Execute("cycles.run fake n=1 m=2");
    EXPECT_EQ("ERR usage\r\n", Output());
}
