#include "validation/firmware/cycles/CycleRun.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace validation
{
    namespace
    {
        constexpr int32_t success = 0;
        constexpr uint32_t q16 = 16;
        constexpr uint32_t randomSeed = 0x2545f491;

        uint32_t BusyCycles(uint32_t elapsed, uint64_t idle)
        {
            return idle >= elapsed ? 0 : elapsed - static_cast<uint32_t>(idle);
        }
    }

    const std::array<CycleRun::Operation, 12> CycleRun::operations{ {
        { "format", &CycleRun::ResetSubject, nullptr, &CycleRun::Format, nullptr, nullptr },
        { "mount", &CycleRun::SetupFormat, &CycleRun::UnmountAfterFirst, &CycleRun::Mount, nullptr, nullptr },
        { "open", nullptr, nullptr, &CycleRun::OpenSmallFile, &CycleRun::Close, nullptr },
        { "list", nullptr, nullptr, &CycleRun::List, nullptr, nullptr },
        { "read", &CycleRun::OpenBigForRead, nullptr, &CycleRun::ReadAtRandom, nullptr, &CycleRun::Close },
        { "overwrite", &CycleRun::OpenBigForOverwrite, nullptr, &CycleRun::OverwriteAtRandom, nullptr, &CycleRun::Close },
        { "create", nullptr, nullptr, &CycleRun::Create, &CycleRun::Close, nullptr },
        { "append", &CycleRun::OpenLog, nullptr, &CycleRun::Append, nullptr, &CycleRun::Close },
        { "write", &CycleRun::OpenStream, &CycleRun::RewindStream, &CycleRun::WriteStream, nullptr, &CycleRun::Close },
        { "rename", nullptr, nullptr, &CycleRun::Rename, nullptr, nullptr },
        { "remove", nullptr, nullptr, &CycleRun::Remove, nullptr, nullptr },
        { "unmount", nullptr, &CycleRun::MountAfterFirst, &CycleRun::Unmount, nullptr, nullptr },
    } };

    CycleRun::CycleRun(ResultLines& results, const CycleCounter& cycles, const IdleCycleSource& idle, const CyclePlan& plan)
        : results(results)
        , cycles(cycles)
        , idle(idle)
        , plan(plan)
    {
        really_assert(plan.smallFiles >= 1 && plan.smallFileBytes <= writeBytes && plan.appendBytes <= writeBytes);
        really_assert(plan.transferBytes >= 1 && plan.transferBytes <= writeBytes && plan.bigFileBytes % writeBytes == 0 && plan.bigFileBytes >= plan.transferBytes);
        really_assert(plan.rewindEvery >= 1 && plan.pollInterval > infra::Duration::zero() && plan.calibration >= plan.pollInterval);

        for (std::size_t position = 0; position != data.size(); ++position)
            data[position] = static_cast<uint8_t>(position * 7 + 1);
    }

    void CycleRun::Start(CycleSubject& subject, infra::BoundedConstString device, uint32_t runs, const infra::Function<void(bool success)>& onDone)
    {
        really_assert(!Running());
        really_assert(runs >= 1 && runs <= CycleSamples::capacity);

        this->subject = &subject;
        this->device = device;
        this->runs = runs;
        this->onDone = onDone;
        operation = 0;
        random = randomSeed;
        Calibrate();
    }

    bool CycleRun::Running() const
    {
        return static_cast<bool>(onDone);
    }

    CycleSubject::Done CycleRun::Then(Step next)
    {
        resumeWith = next;
        return [this](int32_t status)
        {
            Resume(status);
        };
    }

    void CycleRun::Resume(int32_t status)
    {
        if (status != success)
            Fail(status);
        else
            (this->*resumeWith)();
    }

    void CycleRun::Fail(int32_t status)
    {
        pollTimer.Cancel();
        infra::StringOutputStream::WithStorage<12> code;
        code << status;
        results.Record("cycles.failed").Add("subject", subject->Name()).Add("op", operations[operation].name).Add("run", run).Add("status", code.Storage());
        Finish(false);
    }

    void CycleRun::Finish(bool succeeded)
    {
        onDone(succeeded);
    }

    void CycleRun::RunStep(Step step, Step done)
    {
        stepDone = done;

        if (step != nullptr)
            (this->*step)();
        else
            (this->*done)();
    }

    void CycleRun::Calibrate()
    {
        startCycles = cycles.Cycles();
        startIdle = idle.IdleCycles();
        calibrationPolls = static_cast<uint32_t>(plan.calibration / plan.pollInterval);
        pollTimer.Start(plan.pollInterval, [this]
            {
                if (--calibrationPolls == 0)
                {
                    pollTimer.Cancel();
                    Calibrated();
                }
            });
    }

    void CycleRun::Calibrated()
    {
        auto elapsed = cycles.Cycles() - startCycles;
        auto busy = BusyCycles(elapsed, idle.IdleCycles() - startIdle);
        noiseQ16 = elapsed == 0 ? 0 : static_cast<uint32_t>((uint64_t{ busy } << q16) / elapsed);
        StartOperation();
    }

    void CycleRun::StartOperation()
    {
        run = 0;
        callSamples.Clear();
        settledSamples.Clear();
        RunStep(operations[operation].prepare, &CycleRun::Prepared);
    }

    void CycleRun::Prepared()
    {
        StartRun();
    }

    void CycleRun::StartRun()
    {
        if (run == runs)
            RunStep(operations[operation].finish, &CycleRun::Finished);
        else
            RunStep(operations[operation].before, &CycleRun::BeforeDone);
    }

    void CycleRun::BeforeDone()
    {
        WaitIdle(&CycleRun::Measure);
    }

    void CycleRun::Measure()
    {
        startCycles = cycles.Cycles();
        startIdle = idle.IdleCycles();
        RunStep(operations[operation].measured, &CycleRun::Measured);
    }

    void CycleRun::Measured()
    {
        callCycles = cycles.Cycles() - startCycles;
        callIdle = idle.IdleCycles() - startIdle;
        WaitIdle(&CycleRun::Settled);
    }

    void CycleRun::Settled()
    {
        auto elapsed = cycles.Cycles() - startCycles;
        auto afterElapsed = elapsed - callCycles;
        auto afterBusy = BusyCycles(afterElapsed, idle.IdleCycles() - startIdle - callIdle);
        auto noise = static_cast<uint32_t>((uint64_t{ afterElapsed } * noiseQ16) >> q16);

        callSamples.Add(callCycles);
        settledSamples.Add(BusyCycles(callCycles, callIdle) + (afterBusy > noise ? afterBusy - noise : 0));
        RunStep(operations[operation].after, &CycleRun::AfterDone);
    }

    void CycleRun::AfterDone()
    {
        ++run;
        StartRun();
    }

    void CycleRun::Finished()
    {
        Report();

        if (++operation == operations.size())
            Finish(true);
        else
            StartOperation();
    }

    void CycleRun::Report()
    {
        results.Record("cycles")
            .Add("subject", subject->Name())
            .Add("device", device)
            .Add("op", operations[operation].name)
            .Add("n", runs)
            .Add("ramBytes", subject->RamBytes())
            .Add("minCycles", callSamples.Minimum())
            .Add("medianCycles", callSamples.Median())
            .Add("maxCycles", callSamples.Maximum())
            .Add("settledCycles", settledSamples.Median());
    }

    void CycleRun::WaitIdle(Step next)
    {
        afterIdle = next;

        if (subject->Idle())
            (this->*afterIdle)();
        else
            pollTimer.Start(plan.pollInterval, [this]
                {
                    PollIdle();
                });
    }

    void CycleRun::PollIdle()
    {
        if (subject->Idle())
        {
            pollTimer.Cancel();
            (this->*afterIdle)();
        }
    }

    void CycleRun::ResetSubject()
    {
        subject->Reset(Then(stepDone));
    }

    void CycleRun::SetupFormat()
    {
        subject->Format(Then(&CycleRun::SetupMount));
    }

    void CycleRun::SetupMount()
    {
        subject->Mount(Then(&CycleRun::SetupDirectory));
    }

    void CycleRun::SetupDirectory()
    {
        index = 0;
        subject->MakeDirectory("/d", Then(&CycleRun::SetupNextSmallFile));
    }

    void CycleRun::SetupNextSmallFile()
    {
        if (index == plan.smallFiles)
            SetupBigFile();
        else
        {
            SetPath("/d/f", index);
            subject->Open(path, CycleOpenMode::write, Then(&CycleRun::SetupWriteSmallFile));
        }
    }

    void CycleRun::SetupWriteSmallFile()
    {
        subject->Write(infra::Head(infra::MakeRange(data), plan.smallFileBytes), Then(&CycleRun::SetupCloseSmallFile));
    }

    void CycleRun::SetupCloseSmallFile()
    {
        ++index;
        subject->Close(Then(&CycleRun::SetupNextSmallFile));
    }

    void CycleRun::SetupBigFile()
    {
        index = 0;
        subject->Open("/big", CycleOpenMode::write, Then(&CycleRun::SetupWriteBigFile));
    }

    void CycleRun::SetupWriteBigFile()
    {
        if (index == plan.bigFileBytes / writeBytes)
            subject->Close(Then(&CycleRun::SetupUnmount));
        else
        {
            ++index;
            subject->Write(infra::MakeRange(data), Then(&CycleRun::SetupWriteBigFile));
        }
    }

    void CycleRun::SetupUnmount()
    {
        subject->Unmount(Then(stepDone));
    }

    void CycleRun::Format()
    {
        subject->Format(Then(stepDone));
    }

    void CycleRun::Mount()
    {
        subject->Mount(Then(stepDone));
    }

    void CycleRun::UnmountAfterFirst()
    {
        if (run == 0)
            (this->*stepDone)();
        else
            subject->Unmount(Then(stepDone));
    }

    void CycleRun::MountAfterFirst()
    {
        if (run == 0)
            (this->*stepDone)();
        else
            subject->Mount(Then(stepDone));
    }

    void CycleRun::Unmount()
    {
        subject->Unmount(Then(stepDone));
    }

    void CycleRun::OpenSmallFile()
    {
        SetPath("/d/f", run % plan.smallFiles);
        subject->Open(path, CycleOpenMode::read, Then(stepDone));
    }

    void CycleRun::Close()
    {
        subject->Close(Then(stepDone));
    }

    void CycleRun::List()
    {
        subject->List("/d", Then(stepDone));
    }

    void CycleRun::OpenBigForRead()
    {
        subject->Open("/big", CycleOpenMode::read, Then(stepDone));
    }

    void CycleRun::OpenBigForOverwrite()
    {
        subject->Open("/big", CycleOpenMode::readWrite, Then(stepDone));
    }

    void CycleRun::ReadAtRandom()
    {
        subject->Seek(RandomOffset());
        subject->Read(infra::Head(infra::MakeRange(readBuffer), plan.transferBytes), Then(stepDone));
    }

    void CycleRun::OverwriteAtRandom()
    {
        subject->Seek(RandomOffset());
        subject->Write(infra::Head(infra::MakeRange(data), plan.transferBytes), Then(&CycleRun::Sync));
    }

    void CycleRun::Sync()
    {
        subject->Sync(Then(stepDone));
    }

    void CycleRun::Create()
    {
        SetPath("/c", run);
        subject->Open(path, CycleOpenMode::write, Then(stepDone));
    }

    void CycleRun::OpenLog()
    {
        subject->Open("/log", CycleOpenMode::append, Then(stepDone));
    }

    void CycleRun::Append()
    {
        subject->Write(infra::Head(infra::MakeRange(data), plan.appendBytes), Then(&CycleRun::Sync));
    }

    void CycleRun::OpenStream()
    {
        subject->Open("/w", CycleOpenMode::write, Then(stepDone));
    }

    void CycleRun::RewindStream()
    {
        if (run % plan.rewindEvery == 0)
            subject->Seek(0);

        (this->*stepDone)();
    }

    void CycleRun::WriteStream()
    {
        subject->Write(infra::MakeRange(data), Then(stepDone));
    }

    void CycleRun::Rename()
    {
        SetPath("/c", run);
        target.clear();
        infra::StringOutputStream stream{ target };
        stream << "/r" << run;
        subject->Rename(path, target, Then(stepDone));
    }

    void CycleRun::Remove()
    {
        SetPath("/r", run);
        subject->Remove(path, Then(stepDone));
    }

    void CycleRun::SetPath(const char* prefix, uint32_t number)
    {
        path.clear();
        infra::StringOutputStream stream{ path };
        stream << prefix << number;
    }

    uint32_t CycleRun::RandomOffset()
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random % ((plan.bigFileBytes - plan.transferBytes) / plan.transferBytes + 1) * plan.transferBytes;
    }
}
