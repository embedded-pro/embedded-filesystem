#include "validation/firmware/workload/WorkloadRunner.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace validation
{
    WorkloadRunner::WorkloadRunner(ResultLines& results, const CycleCounter& cycles, uint32_t clockHz)
        : report(results, cycles, clockHz)
        , callStopwatch(cycles, clockHz)
    {}

    void WorkloadRunner::Release(const infra::Function<void()>& onReleased)
    {
        really_assert(state != State::running);
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
        really_assert(state == State::idle);
        this->volume = &volume;
        this->flash = &flash;
        this->onInterrupted = onInterrupted;
        state = State::running;
        earlierMounts = VolumeCounts{};
        report.Start(context, flash.Counts(), VolumeNow());
        flash.OnTimeout([this]
            {
                Interrupt();
            });
    }

    void WorkloadRunner::End()
    {
        report.End(flash->Counts(), VolumeNow());
        flash->OnTimeout(nullptr);
        state = State::idle;
    }

    void WorkloadRunner::SetPhase(infra::BoundedConstString name, PhaseWindow window)
    {
        report.StartPhase(name, window, flash->Counts(), VolumeNow());
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
                if (state == State::interrupted)
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
                if (state == State::interrupted)
                    return;

                EndCall(result, 0);
                onUsage(result, usage);
            });
    }

    void WorkloadRunner::Idle(infra::Duration duration, const infra::Function<void()>& onDone)
    {
        onIdle = onDone;
        StartCall(WorkloadOperation::idle, 0, false);
        idle.Wait(duration);
    }

    void WorkloadRunner::Settle(infra::Duration limit, const infra::Function<void()>& onDone)
    {
        onIdle = onDone;
        StartCall(WorkloadOperation::idle, 0, false);
        idle.Settle(volume->FileSystem(), limit);
    }

    uint32_t WorkloadRunner::FileSize() const
    {
        return file != nullptr ? file->Size() : 0;
    }

    bool WorkloadRunner::Running() const
    {
        return state == State::running;
    }

    bool WorkloadRunner::Interrupted() const
    {
        return state == State::interrupted;
    }

    bool WorkloadRunner::Failed() const
    {
        return report.Failed();
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
        report.Add(call, { result, userBytes, latencyNs, flash->Counts(), VolumeNow() });
    }

    void WorkloadRunner::Completed(fs::Result result)
    {
        if (state == State::interrupted)
            return;

        EndCall(result, call.userBytes);
        onDone(result);
    }

    void WorkloadRunner::NextEntry()
    {
        directory->Next([this](fs::Result result, const std::optional<fs::EntryInfo>& entry)
            {
                if (result == fs::Result::success && entry && state != State::interrupted)
                    NextEntry();
                else
                {
                    directory = nullptr;
                    Completed(result);
                }
            });
    }

    void WorkloadRunner::IdleDone()
    {
        EndCall(fs::Result::success, 0);
        onIdle();
    }

    // The device stays claimed until the board is reset, since the request or the call that timed out may still complete
    void WorkloadRunner::Interrupt()
    {
        if (state == State::interrupted)
            return;

        state = State::interrupted;
        callTimer.Cancel();
        idle.Cancel();
        report.TimedOut(call.operation);
        onInterrupted();
    }

    std::optional<VolumeCounts> WorkloadRunner::VolumeNow() const
    {
        auto counters = volume->Counters();
        if (!counters)
            return std::nullopt;

        return earlierMounts + *counters;
    }
}
