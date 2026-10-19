#include "fs/fat/fiber/FatFiberReader.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <utility>

namespace fs
{
    FatFiberReader::FatFiberReader(hal::BlockDevice& device, infra::ByteRange sector, std::size_t transferAlignment, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const PathLimits& limits)
        : runner(device, infra::MakeRange(stackStorage.bytes))
        , window(runner, sector)
        , volume(device, runner, window, transferAlignment, nameBuffer, deferred, [this]
              {
                  if (lifecycle.RemoveMedia())
                      volume.MarkUnmounted(Result::mediaRemoved);
              })
        , limits(limits)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
    {}

    void FatFiberReader::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (!lifecycle.StartMount())
            volume.Defer(onDone, Result::busy);
        else
            BeginChange(onDone);
    }

    void FatFiberReader::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.StartUnmount())
            BeginChange(onDone);
        else
            volume.Defer(onDone, lifecycle.Changing() ? Result::busy : lifecycle.Unavailable());
    }

    void FatFiberReader::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        if (auto refusal = mode == OpenMode::read ? Refusal(path) : ReadOnly(); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onOpen = onDone;
            Start(path);
        }
    }

    void FatFiberReader::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onOpenDirectory = onDone;
            Start(path);
        }
    }

    void FatFiberReader::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onStat = onDone;
            Start(path);
        }
    }

    void FatFiberReader::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatFiberReader::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatFiberReader::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatFiberReader::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        volume.Defer(onDone, lifecycle.IsMounted() ? Result::unsupported : lifecycle.Unavailable());
    }

    bool FatFiberReader::Idle() const
    {
        return volume.Idle();
    }

    std::size_t FatFiberReader::StackPeak() const
    {
        return runner.StackPeak();
    }

    FatFiberVolume& FatFiberReader::Volume()
    {
        return volume;
    }

    Result FatFiberReader::Refusal(infra::BoundedConstString path) const
    {
        if (!lifecycle.IsMounted())
            return lifecycle.Unavailable();

        if (auto validation = ValidatePath(path, limits); validation != Result::success)
            return validation;

        return PathCallPending() ? Result::busy : Result::success;
    }

    Result FatFiberReader::ReadOnly() const
    {
        return lifecycle.IsMounted() ? Result::readOnly : lifecycle.Unavailable();
    }

    bool FatFiberReader::PathCallPending() const
    {
        return onStat || onOpen || onOpenDirectory;
    }

    void FatFiberReader::Start(infra::BoundedConstString path)
    {
        this->path = path;
        pathClaimer.Claim([this]
            {
                runner.Run(pathClaimer, [this]
                    {
                        RunPath();
                    });
            });
    }

    void FatFiberReader::RunPath()
    {
        pathResult = volume.Available() ? volume.Find(path) : lifecycle.Unavailable();
        volume.Report(pathResult);
        infra::EventDispatcher::Instance().Schedule([this]
            {
                PathCompleted();
            });
    }

    void FatFiberReader::PathCompleted()
    {
        if (onStat)
            StatFound(pathResult);
        else if (onOpen)
            OpenFound(pathResult);
        else
            OpenDirectoryFound(pathResult);
    }

    void FatFiberReader::StatFound(Result result)
    {
        if (result != Result::success)
            onStat(result, EntryInfo{});
        else if (volume.FoundRoot())
            onStat(result, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            onStat(result, InfoOf(volume.Found()));
    }

    void FatFiberReader::OpenFound(Result result)
    {
        infra::SharedPtr<FatFiberFile> file;

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

    void FatFiberReader::OpenDirectoryFound(Result result)
    {
        infra::SharedPtr<FatFiberDirectory> directory;

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

    void FatFiberReader::BeginChange(const infra::Function<void(Result result)>& onDone)
    {
        onChanged = onDone;
        lifecycleClaimer.Claim([this]
            {
                if (lifecycle.Mounting())
                    runner.Run(lifecycleClaimer, [this]
                        {
                            RunMount();
                        });
                else
                {
                    lifecycleClaimer.Release();
                    lifecycle.Unmounted();
                    volume.MarkUnmounted(Result::notMounted);
                    Entered(Result::success);
                }
            });
    }

    void FatFiberReader::RunMount()
    {
        window.Forget();
        auto result = volume.Mount();
        lifecycle.Mounted(result);

        if (result == Result::success)
            volume.MarkMounted();
        else
            volume.MarkUnmounted(result);

        Entered(result);
    }

    void FatFiberReader::Entered(Result result)
    {
        volume.Defer(std::exchange(onChanged, nullptr), result);
    }
}
