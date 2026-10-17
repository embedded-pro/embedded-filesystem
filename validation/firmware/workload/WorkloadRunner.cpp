#include "validation/firmware/workload/WorkloadRunner.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "validation/firmware/workload/CountRecords.hpp"

namespace validation
{
    WorkloadRunner::WorkloadRunner(ResultLines& results, const CycleCounter& cycles, uint32_t clockHz)
        : results(results)
        , callStopwatch(cycles, clockHz)
        , phaseStopwatch(cycles, clockHz)
    {}

    void WorkloadRunner::Release(const infra::Function<void()>& onReleased)
    {
        really_assert(!running);
        file = nullptr;
        directory = nullptr;

        if (mountedVolume == nullptr)
        {
            onReleased();
            return;
        }

        this->onReleased = onReleased;
        mountedVolume->FileSystem().Unmount([this](fs::Result)
            {
                mountedVolume = nullptr;
                this->onReleased();
            });
    }

    void WorkloadRunner::Begin(WorkloadVolume& volume, CountingFlash& flash, const WorkloadContext& context, const infra::Function<void()>& onInterrupted)
    {
        really_assert(!running && !interrupted);
        this->volume = &volume;
        this->flash = &flash;
        this->context = context;
        this->onInterrupted = onInterrupted;
        running = true;
        failure.reset();
        earlierMounts = VolumeCounts{};
        window = PhaseWindow::none;
        firstLog.Clear();
        secondLog.Clear();
        phase.Start("", flash.Counts(), VolumeNow());
        flash.OnTimeout([this]
            {
                Interrupt();
            });
        PrintStart();
    }

    void WorkloadRunner::End()
    {
        PrintPhase();
        PrintWindow("first", firstLog.Window());
        PrintWindow("second", secondLog.Window());
        PrintFailure();
        PrintDone();
        flash->OnTimeout(nullptr);
        running = false;
    }

    void WorkloadRunner::SetPhase(infra::BoundedConstString name, PhaseWindow window)
    {
        PrintPhase();
        this->window = window;
        phase.Start(name, flash->Counts(), VolumeNow());
        phaseStopwatch.Start();
    }

    void WorkloadRunner::Format(const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::format);
        volume->Format([this](fs::Result result)
            {
                Completed(result);
            });
    }

    void WorkloadRunner::Mount(const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::mount);
        countedBeforeMount = VolumeNow();
        volume->FileSystem().Mount([this](fs::Result result)
            {
                if (result == fs::Result::success)
                {
                    mountedVolume = volume;
                    earlierMounts = countedBeforeMount.value_or(VolumeCounts{});
                }

                Completed(result);
            });
    }

    void WorkloadRunner::Unmount(const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::unmount);
        volume->FileSystem().Unmount([this](fs::Result result)
            {
                if (result == fs::Result::success)
                    mountedVolume = nullptr;

                Completed(result);
            });
    }

    void WorkloadRunner::Open(infra::BoundedConstString path, fs::OpenMode mode, const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::open);
        volume->FileSystem().Open(path, mode, [this](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                file = opened;
                Completed(result);
            });
    }

    void WorkloadRunner::Read(infra::ByteRange buffer, const infra::Function<void(fs::Result result, std::size_t bytesRead)>& onDone)
    {
        onRead = onDone;
        StartCall(WorkloadOperation::read);
        file->Read(buffer, [this](fs::Result result, std::size_t bytesRead)
            {
                if (interrupted)
                    return;

                EndCall(result, bytesRead);
                onRead(result, bytesRead);
            });
    }

    void WorkloadRunner::Write(infra::ConstByteRange data, const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::write, data.size());
        file->Write(data, [this](fs::Result result)
            {
                Completed(result);
            });
    }

    void WorkloadRunner::Seek(uint32_t position)
    {
        StartCall(WorkloadOperation::seek);
        file->Seek(position);
        EndCall(fs::Result::success, 0);
    }

    void WorkloadRunner::Sync(const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::sync);
        file->Sync([this](fs::Result result)
            {
                Completed(result);
            });
    }

    void WorkloadRunner::Close(const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::close);
        file->Close([this](fs::Result result)
            {
                file = nullptr;
                Completed(result);
            });
    }

    void WorkloadRunner::ExpectSize(uint32_t size, const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::expectSize);
        file->ExpectSize(size, [this](fs::Result result)
            {
                Completed(result);
            });
    }

    void WorkloadRunner::Remove(infra::BoundedConstString path, const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::remove);
        volume->FileSystem().Remove(path, [this](fs::Result result)
            {
                Completed(result);
            });
    }

    void WorkloadRunner::MakeDirectory(infra::BoundedConstString path, const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::makeDirectory);
        volume->FileSystem().MakeDirectory(path, [this](fs::Result result)
            {
                Completed(result);
            });
    }

    void WorkloadRunner::List(infra::BoundedConstString path, const Done& onDone)
    {
        this->onDone = onDone;
        StartCall(WorkloadOperation::list);
        volume->FileSystem().OpenDirectory(path, [this](fs::Result result, infra::SharedPtr<fs::Directory> opened)
            {
                directory = opened;
                if (result == fs::Result::success)
                    NextEntry();
                else
                    Completed(result);
            });
    }

    void WorkloadRunner::Usage(const infra::Function<void(fs::Result result, fs::VolumeUsage usage)>& onDone)
    {
        onUsage = onDone;
        StartCall(WorkloadOperation::usage);
        volume->FileSystem().Usage([this](fs::Result result, fs::VolumeUsage usage)
            {
                if (interrupted)
                    return;

                EndCall(result, 0);
                onUsage(result, usage);
            });
    }

    void WorkloadRunner::Idle(infra::Duration duration, const infra::Function<void()>& onDone)
    {
        onIdle = onDone;
        StartCall(WorkloadOperation::idle, 0, false);
        idleTimer.Start(duration, [this]
            {
                IdleDone();
            });
    }

    void WorkloadRunner::Settle(infra::Duration limit, const infra::Function<void()>& onDone)
    {
        onIdle = onDone;
        StartCall(WorkloadOperation::idle, 0, false);
        settleDeadline = infra::Now() + limit;
        pollTimer.Start(pollInterval, [this]
            {
                PollIdle();
            });
    }

    uint32_t WorkloadRunner::FileSize() const
    {
        return file != nullptr ? file->Size() : 0;
    }

    bool WorkloadRunner::Running() const
    {
        return running;
    }

    bool WorkloadRunner::Interrupted() const
    {
        return interrupted;
    }

    bool WorkloadRunner::Failed() const
    {
        return failure.has_value();
    }

    void WorkloadRunner::StartCall(WorkloadOperation operation, uint64_t userBytes, bool limited)
    {
        call = { operation, userBytes, FileSize(), flash->Counts(), VolumeNow() };
        if (limited)
            callTimer.Start(callLimit, [this]
                {
                    Interrupt();
                });
        callStopwatch.Start();
    }

    void WorkloadRunner::EndCall(fs::Result result, uint64_t userBytes)
    {
        auto latencyNs = callStopwatch.ElapsedNs();
        callTimer.Cancel();
        phase.Add(call.operation, userBytes, latencyNs);
        AddToWindow();
        Check(result);
    }

    void WorkloadRunner::Completed(fs::Result result)
    {
        if (interrupted)
            return;

        EndCall(result, call.userBytes);
        onDone(result);
    }

    void WorkloadRunner::NextEntry()
    {
        directory->Next([this](fs::Result result, std::optional<fs::EntryInfo> entry)
            {
                if (result == fs::Result::success && entry && !interrupted)
                    NextEntry();
                else
                {
                    directory = nullptr;
                    Completed(result);
                }
            });
    }

    void WorkloadRunner::PollIdle()
    {
        if (volume->FileSystem().Idle() || infra::Now() >= settleDeadline)
        {
            pollTimer.Cancel();
            IdleDone();
        }
    }

    void WorkloadRunner::IdleDone()
    {
        EndCall(fs::Result::success, 0);
        onIdle();
    }

    void WorkloadRunner::AddToWindow()
    {
        auto volumeNow = VolumeNow();
        if (window == PhaseWindow::none || !IsPartOfAppend(call.operation) || !volumeNow || !call.volume)
            return;

        AppendCall append{ call.operation, call.fileSizeBefore, flash->Counts() - call.device, *volumeNow - *call.volume };
        if (window == PhaseWindow::firstLog)
            firstLog.Add(append);
        else
            secondLog.Add(append);
    }

    void WorkloadRunner::Check(fs::Result result)
    {
        auto expected = result == fs::Result::success || (call.operation == WorkloadOperation::expectSize && result == fs::Result::unsupported);
        if (expected || failure)
            return;

        failure.emplace();
        failure->phase = phase.Name();
        failure->operation = call.operation;
        infra::StringOutputStream stream{ failure->result };
        stream << result;
    }

    // The device stays claimed until the board is reset, since the request or the call that timed out may still complete
    void WorkloadRunner::Interrupt()
    {
        if (interrupted)
            return;

        interrupted = true;
        callTimer.Cancel();
        idleTimer.Cancel();
        pollTimer.Cancel();
        failure.emplace();
        failure->phase = phase.Name();
        failure->operation = call.operation;
        failure->result = "timeout";
        PrintFailure();
        running = false;
        onInterrupted();
    }

    void WorkloadRunner::PrintStart() const
    {
        results.Record("workload")
            .Add("device", context.device)
            .Add("subject", context.subject)
            .Add("workload", context.workload)
            .Add("fill", context.fill)
            .Add("scale", context.scale)
            .Add("ramBytes", context.ramBytes);
    }

    void WorkloadRunner::PrintPhase()
    {
        if (!phase.Name().empty())
            phase.Print(results, context, { phaseStopwatch.ElapsedNs(), flash->Counts(), VolumeNow() });
    }

    void WorkloadRunner::PrintWindow(const char* log, const std::optional<WindowTotals>& totals)
    {
        if (!totals)
            return;

        auto record = results.Record("window");
        record.Add("device", context.device)
            .Add("subject", context.subject)
            .Add("workload", context.workload)
            .Add("fill", context.fill)
            .Add("log", log)
            .Add("appends", totals->appends);
        AddCounts(record, totals->device);
        AddCounts(record, totals->volume);
    }

    void WorkloadRunner::PrintFailure() const
    {
        if (!failure)
            return;

        results.Record("workload.failed")
            .Add("device", context.device)
            .Add("subject", context.subject)
            .Add("workload", context.workload)
            .Add("fill", context.fill)
            .Add("phase", failure->phase)
            .Add("op", OperationName(failure->operation))
            .Add("result", failure->result);
    }

    void WorkloadRunner::PrintDone() const
    {
        results.Record("workload.done")
            .Add("device", context.device)
            .Add("subject", context.subject)
            .Add("workload", context.workload)
            .Add("fill", context.fill);
    }

    std::optional<VolumeCounts> WorkloadRunner::VolumeNow() const
    {
        auto counters = volume->Counters();
        if (!counters)
            return std::nullopt;

        return earlierMounts + *counters;
    }
}
