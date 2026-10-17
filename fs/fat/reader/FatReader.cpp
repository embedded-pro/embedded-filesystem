#include "fs/fat/reader/FatReader.hpp"
#include <utility>

namespace fs
{
    FatReader::FatReader(hal::BlockDevice& device, infra::ByteRange sector, std::size_t transferAlignment, infra::ByteRange nameBuffer, FatEntryAssembler::Reading reading, DeferredCompletions& deferred, const PathLimits& limits)
        : window(device, sector)
        , volume(device, window, transferAlignment, nameBuffer, reading, deferred, [this]
              {
                  lifecycle.Dispatch(FatMediaRemoved{});
              })
        , mount(device, window, volume.Geometry())
        , limits(limits)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
        , lifecycle(*this, services::Validated<FatReader>())
    {
        lifecycle.OnEntered<FatMounted>([](FatReader& reader, FatMounted&)
            {
                reader.volume.MarkMounted();
                reader.Entered(Result::success);
            });
        lifecycle.OnEntered<FatFault>([](FatReader& reader, const FatFault& fault)
            {
                reader.volume.MarkUnmounted(fault.result);
                reader.Entered(fault.result);
            });
        lifecycle.OnEntered<FatUnmounted>([](FatReader& reader, FatUnmounted&)
            {
                reader.volume.MarkUnmounted(Result::notMounted);
                reader.Entered(Result::success);
            });
        lifecycle.Start<FatUnmounted>();
    }

    void FatReader::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            volume.Defer(onDone, Result::busy);
    }

    void FatReader::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<FatMounting>(lifecycle.CurrentState()) || std::holds_alternative<FatUnmounting>(lifecycle.CurrentState()))
            volume.Defer(onDone, Result::busy);
        else
            volume.Defer(onDone, Unavailable());
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
        volume.Defer(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    bool FatReader::Idle() const
    {
        return volume.Idle();
    }

    FatReaderVolume& FatReader::Volume()
    {
        return volume;
    }

    bool FatReader::IsMounted() const
    {
        return std::holds_alternative<FatMounted>(lifecycle.CurrentState());
    }

    Result FatReader::Unavailable() const
    {
        if (const auto* fault = std::get_if<FatFault>(&lifecycle.CurrentState()); fault != nullptr && fault->result == Result::mediaRemoved)
            return Result::mediaRemoved;

        return Result::notMounted;
    }

    Result FatReader::Refusal(infra::BoundedConstString path) const
    {
        if (!IsMounted())
            return Unavailable();

        if (auto validation = ValidatePath(path, limits); validation != Result::success)
            return validation;

        return PathCallPending() ? Result::busy : Result::success;
    }

    Result FatReader::ReadOnly() const
    {
        return IsMounted() ? Result::readOnly : Unavailable();
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
            volume.Walk().Find(path, [this](Result result)
                {
                    Found(result);
                });
        else
            Found(Unavailable());
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
        const auto& walk = volume.Walk();

        if (result != Result::success)
            onStat(result, EntryInfo{});
        else if (walk.FoundRoot())
            onStat(result, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            onStat(result, InfoOf(walk.Found()));
    }

    void FatReader::OpenFound(Result result)
    {
        const auto& walk = volume.Walk();
        infra::SharedPtr<FatReaderFile> file;

        if (result == Result::success && walk.FoundDirectory())
            result = Result::isADirectory;
        else if (result == Result::success && walk.Found().entry.size != 0 && !volume.Geometry().IsCluster(walk.Found().entry.firstCluster))
            result = Result::corrupted;
        else if (result == Result::success)
            file = AllocateFile();

        if (file != nullptr)
            file->Open(walk.Found());
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        onOpen(result, file);
    }

    void FatReader::OpenDirectoryFound(Result result)
    {
        const auto& walk = volume.Walk();
        infra::SharedPtr<FatReaderDirectory> directory;

        if (result == Result::success && !walk.FoundDirectory())
            result = Result::notADirectory;
        else if (result == Result::success && !walk.FoundRoot() && !volume.Geometry().IsCluster(walk.FoundCluster()))
            result = Result::corrupted;
        else if (result == Result::success)
            directory = AllocateDirectory();

        if (directory != nullptr)
            directory->Open(walk.FoundCluster());
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        onOpenDirectory(result, directory);
    }

    void FatReader::BeginMount()
    {
        lifecycleClaimer.Claim([this]
            {
                window.Forget();
                mount.Start([this](Result result)
                    {
                        Mounted(result);
                    });
            });
    }

    void FatReader::Mounted(Result result)
    {
        lifecycleClaimer.Release();
        lifecycle.Dispatch(FatMountDone{ result });
    }

    void FatReader::BeginUnmount()
    {
        lifecycleClaimer.Claim([this]
            {
                lifecycleClaimer.Release();
                lifecycle.Dispatch(FatUnmountDone{});
            });
    }

    void FatReader::Entered(Result result)
    {
        if (onMountDone)
            volume.Defer(std::exchange(onMountDone, nullptr), result);
        if (onUnmountDone)
            volume.Defer(std::exchange(onUnmountDone, nullptr), result);
    }
}
