#include "fs/fat/reader/FatReader.hpp"
#include <utility>

namespace fs
{
    FatReader::FatReader(hal::BlockDevice& device, infra::ByteRange sector, std::size_t transferAlignment, infra::ByteRange nameBuffer, FatEntryAssembler::Reading reading, DeferredCompletions& deferred, const PathLimits& limits)
        : window(device, sector)
        , volume(device, window, transferAlignment, nameBuffer, reading, deferred, [this]
              {
                  if (lifecycle.RemoveMedia())
                      volume.MarkUnmounted(Result::mediaRemoved);
              })
        , search(volume.Reader(), volume.Geometry())
        , mount(device, window, volume.Geometry())
        , limits(limits)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
    {}

    void FatReader::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (!lifecycle.StartMount())
            volume.Defer(onDone, Result::busy);
        else
            BeginChange(onDone);
    }

    void FatReader::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.StartUnmount())
            BeginChange(onDone);
        else
            volume.Defer(onDone, lifecycle.Changing() ? Result::busy : lifecycle.Unavailable());
    }

    void FatReader::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        if (auto refusal = mode == OpenMode::read ? Refusal(path) : ReadOnly(); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onOpen = onDone;
            Start(path);
        }
    }

    void FatReader::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onOpenDirectory = onDone;
            Start(path);
        }
    }

    void FatReader::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onStat = onDone;
            Start(path);
        }
    }

    void FatReader::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatReader::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatReader::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, ReadOnly());
    }

    void FatReader::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        volume.Defer(onDone, lifecycle.IsMounted() ? Result::unsupported : lifecycle.Unavailable());
    }

    bool FatReader::Idle() const
    {
        return volume.Idle();
    }

    FatReaderVolume& FatReader::Volume()
    {
        return volume;
    }

    Result FatReader::Refusal(infra::BoundedConstString path) const
    {
        if (!lifecycle.IsMounted())
            return lifecycle.Unavailable();

        if (auto validation = ValidatePath(path, limits); validation != Result::success)
            return validation;

        return PathCallPending() ? Result::busy : Result::success;
    }

    Result FatReader::ReadOnly() const
    {
        return lifecycle.IsMounted() ? Result::readOnly : lifecycle.Unavailable();
    }

    bool FatReader::PathCallPending() const
    {
        return onStat || onOpen || onOpenDirectory;
    }

    void FatReader::Start(infra::BoundedConstString path)
    {
        this->path = path;
        pathClaimer.Claim([this]
            {
                Run();
            });
    }

    void FatReader::Run()
    {
        if (volume.Available())
            search.Find(path, [this](Result result)
                {
                    Found(result);
                });
        else
            Found(lifecycle.Unavailable());
    }

    void FatReader::Found(Result result)
    {
        pathClaimer.Release();
        volume.Report(result);

        if (onStat)
            StatFound(result);
        else if (onOpen)
            OpenFound(result);
        else
            OpenDirectoryFound(result);
    }

    void FatReader::StatFound(Result result)
    {
        if (result != Result::success)
            onStat(result, EntryInfo{});
        else if (search.FoundRoot())
            onStat(result, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            onStat(result, InfoOf(search.Found()));
    }

    void FatReader::OpenFound(Result result)
    {
        infra::SharedPtr<FatReaderFile> file;

        if (result == Result::success && search.FoundDirectory())
            result = Result::isADirectory;
        else if (result == Result::success && search.Found().entry.size != 0 && !volume.Geometry().IsCluster(search.Found().entry.firstCluster))
            result = Result::corrupted;
        else if (result == Result::success)
            file = AllocateFile();

        if (file != nullptr)
            file->Open(search.Found());
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        onOpen(result, file);
    }

    void FatReader::OpenDirectoryFound(Result result)
    {
        infra::SharedPtr<FatReaderDirectory> directory;

        if (result == Result::success && !search.FoundDirectory())
            result = Result::notADirectory;
        else if (result == Result::success && !search.FoundRoot() && !volume.Geometry().IsCluster(search.FoundCluster()))
            result = Result::corrupted;
        else if (result == Result::success)
            directory = AllocateDirectory();

        if (directory != nullptr)
            directory->Open(search.FoundCluster());
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        onOpenDirectory(result, directory);
    }

    void FatReader::BeginChange(const infra::Function<void(Result result)>& onDone)
    {
        onChanged = onDone;
        lifecycleClaimer.Claim([this]
            {
                if (lifecycle.Mounting())
                {
                    window.Forget();
                    mount.Start([this](Result result)
                        {
                            Mounted(result);
                        });
                }
                else
                {
                    lifecycleClaimer.Release();
                    lifecycle.Unmounted();
                    volume.MarkUnmounted(Result::notMounted);
                    Entered(Result::success);
                }
            });
    }

    void FatReader::Mounted(Result result)
    {
        lifecycleClaimer.Release();
        lifecycle.Mounted(result);

        if (result == Result::success)
            volume.MarkMounted();
        else
            volume.MarkUnmounted(result);

        Entered(result);
    }

    void FatReader::Entered(Result result)
    {
        volume.Defer(std::exchange(onChanged, nullptr), result);
    }
}
