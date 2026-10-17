#include "fs/fat/coro/FatCoroReader.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <utility>

namespace fs
{
    FatCoroReader::FatCoroReader(hal::BlockDevice& device, infra::ByteRange sector, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const PathLimits& limits)
        : window(device, sector)
        , volume(device, window, nameBuffer, deferred, [this]
              {
                  lifecycle.Dispatch(FatMediaRemoved{});
              })
        , limits(limits)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
        , lifecycle(*this, services::Validated<FatCoroReader>())
    {
        lifecycle.OnEntered<FatMounted>([](FatCoroReader& reader, FatMounted&)
            {
                reader.volume.MarkMounted();
                reader.Entered(Result::success);
            });
        lifecycle.OnEntered<FatFault>([](FatCoroReader& reader, const FatFault& fault)
            {
                reader.volume.MarkUnmounted(fault.result);
                reader.Entered(fault.result);
            });
        lifecycle.OnEntered<FatUnmounted>([](FatCoroReader& reader, FatUnmounted&)
            {
                reader.volume.MarkUnmounted(Result::notMounted);
                reader.Entered(Result::success);
            });
        lifecycle.Start<FatUnmounted>();
    }

    void FatCoroReader::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            volume.Defer(onDone, Result::busy);
    }

    void FatCoroReader::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<FatMounting>(lifecycle.CurrentState()) || std::holds_alternative<FatUnmounting>(lifecycle.CurrentState()))
            volume.Defer(onDone, Result::busy);
        else
            volume.Defer(onDone, Unavailable());
    }

    void FatCoroReader::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        if (auto refusal = mode == OpenMode::read ? Refusal(path) : ReadOnly(); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onOpen = onDone;
            Start(path);
        }
    }

    void FatCoroReader::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onOpenDirectory = onDone;
            Start(path);
        }
    }

    void FatCoroReader::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onStat = onDone;
            Start(path);
        }
    }

    void FatCoroReader::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatCoroReader::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatCoroReader::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatCoroReader::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        volume.Defer(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    bool FatCoroReader::Idle() const
    {
        return volume.Idle();
    }

    std::size_t FatCoroReader::FramePeak() const
    {
        return frames.Peak();
    }

    FatCoroVolume& FatCoroReader::Volume()
    {
        return volume;
    }

    bool FatCoroReader::IsMounted() const
    {
        return std::holds_alternative<FatMounted>(lifecycle.CurrentState());
    }

    Result FatCoroReader::Unavailable() const
    {
        if (const auto* fault = std::get_if<FatFault>(&lifecycle.CurrentState()); fault != nullptr && fault->result == Result::mediaRemoved)
            return Result::mediaRemoved;

        return Result::notMounted;
    }

    Result FatCoroReader::Refusal(infra::BoundedConstString path) const
    {
        if (!IsMounted())
            return Unavailable();

        if (auto validation = ValidatePath(path, limits); validation != Result::success)
            return validation;

        return PathCallPending() ? Result::busy : Result::success;
    }

    Result FatCoroReader::ReadOnly() const
    {
        return IsMounted() ? Result::readOnly : Unavailable();
    }

    bool FatCoroReader::PathCallPending() const
    {
        return onStat || onOpen || onOpenDirectory;
    }

    void FatCoroReader::Start(infra::BoundedConstString path)
    {
        this->path = path;
        RunPath(frames);
    }

    coro::Detached FatCoroReader::RunPath(coro::Frames&)
    {
        co_await coro::Claim{ pathClaimer };

        if (volume.Available())
            pathResult = co_await volume.Find(frames, path);
        else
            pathResult = Unavailable();

        pathClaimer.Release();
        volume.Report(pathResult);
        infra::EventDispatcher::Instance().Schedule([this]
            {
                PathCompleted();
            });
    }

    void FatCoroReader::PathCompleted()
    {
        if (onStat)
            StatFound(pathResult);
        else if (onOpen)
            OpenFound(pathResult);
        else
            OpenDirectoryFound(pathResult);
    }

    void FatCoroReader::StatFound(Result result)
    {
        if (result != Result::success)
            onStat(result, EntryInfo{});
        else if (volume.FoundRoot())
            onStat(result, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            onStat(result, InfoOf(volume.Found()));
    }

    void FatCoroReader::OpenFound(Result result)
    {
        infra::SharedPtr<FatCoroFile> file;

        if (result == Result::success && volume.FoundDirectory())
            result = Result::isADirectory;
        else if (result == Result::success && volume.Found().entry.size != 0 && !volume.Geometry().IsCluster(volume.Found().entry.firstCluster))
            result = Result::corrupted;
        else if (result == Result::success)
            file = AllocateFile();

        if (file != nullptr)
            file->Open(volume.Found());
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        onOpen(result, file);
    }

    void FatCoroReader::OpenDirectoryFound(Result result)
    {
        infra::SharedPtr<FatCoroDirectory> directory;

        if (result == Result::success && !volume.FoundDirectory())
            result = Result::notADirectory;
        else if (result == Result::success && !volume.FoundRoot() && !volume.Geometry().IsCluster(volume.FoundCluster()))
            result = Result::corrupted;
        else if (result == Result::success)
            directory = AllocateDirectory();

        if (directory != nullptr)
            directory->Open(volume.FoundCluster());
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        onOpenDirectory(result, directory);
    }

    void FatCoroReader::BeginMount()
    {
        RunMount(frames);
    }

    coro::Detached FatCoroReader::RunMount(coro::Frames&)
    {
        co_await coro::Claim{ lifecycleClaimer };
        window.Forget();
        auto result = co_await volume.Mount(frames);
        lifecycleClaimer.Release();
        lifecycle.Dispatch(FatMountDone{ result });
    }

    void FatCoroReader::BeginUnmount()
    {
        lifecycleClaimer.Claim([this]
            {
                lifecycleClaimer.Release();
                lifecycle.Dispatch(FatUnmountDone{});
            });
    }

    void FatCoroReader::Entered(Result result)
    {
        if (onMountDone)
            volume.Defer(std::exchange(onMountDone, nullptr), result);
        if (onUnmountDone)
            volume.Defer(std::exchange(onUnmountDone, nullptr), result);
    }
}
