#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/cycles/CycleRun.hpp"
#include "gmock/gmock.h"
#include <chrono>
#include <cstdint>
#include <optional>

using namespace std::chrono_literals;

namespace
{
    // Cycles at one per microsecond of the clock of the fixture, plus the busy cycles the subject adds
    class FakeCycleCounter
        : public validation::CycleCounter
    {
    public:
        uint32_t Cycles() const override
        {
            return static_cast<uint32_t>(added + std::chrono::duration_cast<std::chrono::microseconds>(infra::Now() - epoch).count());
        }

        infra::TimePoint epoch{ infra::Now() };
        uint64_t added{ 0 };
    };

    // The part of the clock of the fixture that the processor sleeps, in permille
    class FakeIdleCycles
        : public validation::IdleCycleSource
    {
    public:
        uint64_t IdleCycles() const override
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(infra::Now() - epoch).count()) * idlePermille / 1000;
        }

        infra::TimePoint epoch{ infra::Now() };
        uint64_t idlePermille{ 0 };
    };

    class FakeCycleSubject
        : public validation::CycleSubject
    {
    public:
        explicit FakeCycleSubject(FakeCycleCounter& counter)
            : counter(counter)
        {}

        infra::BoundedConstString Name() const override
        {
            return "fake";
        }

        std::size_t RamBytes() const override
        {
            return 1234;
        }

        bool Idle() const override
        {
            if (busyPollsLeft == 0)
                return true;

            --busyPollsLeft;
            counter.added += busyCyclesPerPoll;
            return false;
        }

        void Reset(const Done& onDone) override
        {
            Call("R", onDone);
        }

        void Format(const Done& onDone) override
        {
            Call("F", onDone);
        }

        void Mount(const Done& onDone) override
        {
            Call("M", onDone);
        }

        void Unmount(const Done& onDone) override
        {
            Call("U", onDone);
        }

        void MakeDirectory(infra::BoundedConstString path, const Done& onDone) override
        {
            Log("D").Log(path);
            Complete(onDone);
        }

        void Open(infra::BoundedConstString path, validation::CycleOpenMode mode, const Done& onDone) override
        {
            static constexpr const char* modes[] = { "r", "w", "a", "+" };
            Log("O").Log(path).Log(modes[static_cast<std::size_t>(mode)]);
            Complete(onDone);
        }

        void Seek(uint32_t position) override
        {
            Log("S");
            counter.added += cyclesPerCall;
            seeks.push_back(position);
        }

        void Read(infra::ByteRange buffer, const Done& onDone) override
        {
            Log("r").Log(buffer.size());
            Complete(onDone);
        }

        void Write(infra::ConstByteRange data, const Done& onDone) override
        {
            Log("w").Log(data.size());
            Complete(onDone);
        }

        void Sync(const Done& onDone) override
        {
            Call("s", onDone);
        }

        void Close(const Done& onDone) override
        {
            Call("C", onDone);
        }

        void List(infra::BoundedConstString path, const Done& onDone) override
        {
            Log("L").Log(path);
            Complete(onDone);
        }

        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone) override
        {
            Log("N").Log(from).Log(">").Log(to);
            Complete(onDone);
        }

        void Remove(infra::BoundedConstString path, const Done& onDone) override
        {
            Log("X").Log(path);
            Complete(onDone);
        }

        infra::BoundedString::WithStorage<2048> log;
        infra::BoundedVector<uint32_t>::WithMaxSize<64> seeks;
        std::optional<std::size_t> failAtCall;
        int32_t failure{ -28 };
        uint32_t cyclesPerCall{ 0 };
        uint32_t busyPollsAfterEachCall{ 0 };
        uint32_t busyCyclesPerPoll{ 0 };

    private:
        FakeCycleSubject& Log(infra::BoundedConstString text)
        {
            log.append(text);
            return *this;
        }

        FakeCycleSubject& Log(std::size_t number)
        {
            infra::StringOutputStream stream{ log };
            stream << static_cast<uint32_t>(number);
            return *this;
        }

        void Call(const char* token, const Done& onDone)
        {
            Log(token);
            Complete(onDone);
        }

        void Complete(const Done& onDone)
        {
            log.append(" ");
            counter.added += cyclesPerCall;
            busyPollsLeft = busyPollsAfterEachCall;
            pendingStatus = failAtCall == calls ? failure : 0;
            pending = onDone;
            ++calls;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    pending(pendingStatus);
                });
        }

        FakeCycleCounter& counter;
        infra::AutoResetFunction<void(int32_t status)> pending;
        int32_t pendingStatus{ 0 };
        mutable uint32_t busyPollsLeft{ 0 };
        std::size_t calls{ 0 };
    };

    validation::CyclePlan SmallPlan()
    {
        validation::CyclePlan plan;
        plan.smallFiles = 2;
        plan.bigFileBytes = 2 * validation::CycleRun::writeBytes;
        return plan;
    }

    class CycleRunTest
        : public services::HilFixture
    {
    public:
        void Run(uint32_t runs, bool success = true)
        {
            run.Start(subject, "ramdisk", runs, [this](bool succeeded)
                {
                    done.callback(succeeded);
                });
            EXPECT_CALL(done, callback(success));
            ForwardTime(10s);
        }

        FakeCycleCounter counter;
        FakeIdleCycles idle;
        FakeCycleSubject subject{ counter };
        validation::ResultLines results{ response };
        validation::CycleRun run{ results, counter, idle, SmallPlan() };
        testing::StrictMock<infra::MockCallback<void(bool)>> done;
    };
}

TEST_F(CycleRunTest, runs_the_operations_of_f2_after_setting_up_the_volume)
{
    Run(2);

    EXPECT_EQ("R F F "
              "F M D/d O/d/f0w w64 C O/d/f1w w64 C O/bigw w4096 w4096 C U "
              "M U M "
              "O/d/f0r C O/d/f1r C "
              "L/d L/d "
              "O/bigr Sr512 Sr512 C "
              "O/big+ Sw512 s Sw512 s C "
              "O/c0w C O/c1w C "
              "O/loga w64 s w64 s C "
              "O/ww Sw4096 w4096 C "
              "N/c0>/r0 N/c1>/r1 "
              "X/r0 X/r1 "
              "U M U ",
        subject.log);
}

TEST_F(CycleRunTest, prints_one_cycles_record_per_operation)
{
    Run(2);

    const auto output = Output();
    const char* operations[] = { "format", "mount", "open", "list", "read", "overwrite", "create", "append", "write", "rename", "remove", "unmount" };
    std::size_t position = 0;
    for (const char* operation : operations)
    {
        infra::BoundedString::WithStorage<32> key;
        infra::StringOutputStream stream{ key };
        stream << " op=" << operation << " ";
        position = output.find(key.data(), position, key.size());
        EXPECT_NE(std::string::npos, position) << operation;
    }
}

TEST_F(CycleRunTest, a_record_holds_the_subject_its_device_the_runs_its_ram_and_the_cycles_of_the_calls)
{
    subject.cyclesPerCall = 100;

    Run(2);

    EXPECT_THAT(Output(), testing::HasSubstr("EVT cycles id=1 subject=fake device=ramdisk op=format n=2 ramBytes=1234 minCycles=100 medianCycles=100 maxCycles=100 settledCycles=100\r\n"));
}

TEST_F(CycleRunTest, a_measured_call_runs_from_the_call_to_the_completion_of_its_last_request)
{
    subject.cyclesPerCall = 100;

    Run(2);

    EXPECT_THAT(Output(), testing::HasSubstr("op=overwrite n=2 ramBytes=1234 minCycles=300 medianCycles=300 maxCycles=300 settledCycles=300\r\n"));
}

TEST_F(CycleRunTest, the_settled_cycles_add_the_busy_cycles_until_the_subject_is_idle)
{
    idle.idlePermille = 1000;
    subject.cyclesPerCall = 100;
    subject.busyPollsAfterEachCall = 3;
    subject.busyCyclesPerPoll = 500;

    Run(2);

    EXPECT_THAT(Output(), testing::HasSubstr("op=list n=2 ramBytes=1234 minCycles=100 medianCycles=100 maxCycles=100 settledCycles=1600\r\n"));
}

TEST_F(CycleRunTest, the_settled_cycles_leave_out_the_busy_cycles_of_waiting_that_the_calibration_measured)
{
    idle.idlePermille = 500;
    subject.cyclesPerCall = 100;
    subject.busyPollsAfterEachCall = 3;

    Run(2);

    EXPECT_THAT(Output(), testing::HasSubstr("op=list n=2 ramBytes=1234 minCycles=100 medianCycles=100 maxCycles=100 settledCycles=100\r\n"));
}

TEST_F(CycleRunTest, waits_until_the_subject_is_idle_before_each_measured_call)
{
    subject.busyPollsAfterEachCall = 2;

    Run(2);

    EXPECT_EQ("R F F F M", subject.log.substr(0, 9));
    EXPECT_THAT(Output(), testing::HasSubstr("op=unmount n=2"));
}

TEST_F(CycleRunTest, a_failed_call_ends_the_run_with_a_failure_record)
{
    subject.failAtCall = 4;

    Run(2, false);

    EXPECT_EQ("R F F F M ", subject.log);
    EXPECT_THAT(Output(), testing::HasSubstr("EVT cycles.failed id=2 subject=fake op=mount run=0 status=-28\r\n"));
}

TEST_F(CycleRunTest, reads_and_overwrites_whole_transfers_at_varying_offsets_within_the_big_file)
{
    validation::CyclePlan plan;
    plan.smallFiles = 1;
    plan.bigFileBytes = 64 * 512;
    validation::CycleRun wide{ results, counter, idle, plan };
    wide.Start(subject, "ramdisk", 20, [this](bool succeeded)
        {
            done.callback(succeeded);
        });
    EXPECT_CALL(done, callback(true));
    ForwardTime(10s);

    ASSERT_EQ(41, subject.seeks.size());
    for (std::size_t seek = 0; seek != 40; ++seek)
    {
        EXPECT_EQ(0, subject.seeks[seek] % 512);
        EXPECT_LE(subject.seeks[seek] + 512, plan.bigFileBytes);
    }
    EXPECT_NE(subject.seeks[0], subject.seeks[1]);
}

TEST_F(CycleRunTest, is_running_until_it_completes)
{
    EXPECT_FALSE(run.Running());

    run.Start(subject, "ramdisk", 1, [this](bool succeeded)
        {
            done.callback(succeeded);
        });
    EXPECT_TRUE(run.Running());

    EXPECT_CALL(done, callback(true));
    ForwardTime(10s);
    EXPECT_FALSE(run.Running());
}
