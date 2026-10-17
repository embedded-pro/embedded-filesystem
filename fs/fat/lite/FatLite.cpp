#include "fs/fat/lite/FatLite.hpp"
#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatName.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/fat/FatTimestamp.hpp"
#include <utility>

namespace fs
{
    FatLite::FatLite(hal::BlockDevice& device, infra::ByteRange sector, std::size_t transferAlignment, infra::ByteRange nameBuffer, FatEntryAssembler::Reading reading, DeferredCompletions& deferred, const PathLimits& limits)
        : barrier(device)
        , window(barrier, sector)
        , volume(barrier, window, transferAlignment, nameBuffer, reading, deferred, [this]
              {
                  lifecycle.Dispatch(FatMediaRemoved{});
              })
        , mount(device, window, volume.Geometry())
        , scan(volume.Reader(), nullptr)
        , limits(limits)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
        , lifecycle(*this, services::Validated<FatLite>())
    {
        lifecycle.OnEntered<FatMounted>([](FatLite& lite, FatMounted&)
            {
                lite.volume.EnterMounted();
                lite.Entered(Result::success);
            });
        lifecycle.OnEntered<FatFault>([](FatLite& lite, const FatFault& fault)
            {
                lite.volume.EnterUnmounted(fault.result);
                lite.Entered(fault.result);
            });
        lifecycle.OnEntered<FatUnmounted>([](FatLite& lite, FatUnmounted&)
            {
                lite.volume.EnterUnmounted(Result::notMounted);
                lite.Entered(lite.lifecycleResult);
            });
        lifecycle.Start<FatUnmounted>();
    }

    FatLite::~FatLite() = default;

    void FatLite::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            volume.Defer(onDone, Result::busy);
    }

    void FatLite::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<FatMounting>(lifecycle.CurrentState()) || std::holds_alternative<FatUnmounting>(lifecycle.CurrentState()))
            volume.Defer(onDone, Result::busy);
        else
            volume.Defer(onDone, Unavailable());
    }

    void FatLite::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            this->mode = mode;
            onOpen = onDone;
            Start(path);
        }
    }

    void FatLite::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onOpenDirectory = onDone;
            Start(path);
        }
    }

    void FatLite::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
        {
            onStat = onDone;
            Start(path);
        }
    }

    void FatLite::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    void FatLite::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    void FatLite::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    void FatLite::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    bool FatLite::Idle() const
    {
        return volume.Idle();
    }

    FatLiteVolume& FatLite::Volume()
    {
        return volume;
    }

    bool FatLite::IsMounted() const
    {
        return std::holds_alternative<FatMounted>(lifecycle.CurrentState());
    }

    Result FatLite::Unavailable() const
    {
        if (const auto* fault = std::get_if<FatFault>(&lifecycle.CurrentState()); fault != nullptr && fault->result == Result::mediaRemoved)
            return Result::mediaRemoved;

        return Result::notMounted;
    }

    Result FatLite::Unsupported() const
    {
        return IsMounted() ? Result::unsupported : Unavailable();
    }

    Result FatLite::Refusal(infra::BoundedConstString path) const
    {
        if (!IsMounted())
            return Unavailable();

        if (auto validation = ValidatePath(path, limits); validation != Result::success)
            return validation;

        return PathCallPending() ? Result::busy : Result::success;
    }

    bool FatLite::PathCallPending() const
    {
        return onStat || onOpen || onOpenDirectory;
    }

    void FatLite::Start(infra::BoundedConstString path)
    {
        this->path = path;
        pathClaimer.Claim([this]
            {
                Run();
            });
    }

    void FatLite::Run()
    {
        if (!volume.Available())
            Found(Unavailable());
        else if (onOpen)
            FindParent();
        else
            volume.Walk().Find(path, [this](Result result)
                {
                    Found(result);
                });
    }

    void FatLite::Found(Result result)
    {
        pathClaimer.Release();
        volume.Report(result);

        if (onStat)
            StatFound(result);
        else if (onOpen)
            OpenDone(result);
        else
            OpenDirectoryFound(result);
    }

    void FatLite::StatFound(Result result)
    {
        const auto& walk = volume.Walk();

        if (result != Result::success)
            onStat(result, EntryInfo{});
        else if (walk.FoundRoot())
            onStat(result, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            onStat(result, InfoOf(walk.Found()));
    }

    void FatLite::OpenDirectoryFound(Result result)
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

    void FatLite::FindParent()
    {
        if (IsRoot(path))
        {
            Found(Result::isADirectory);
            return;
        }

        auto split = SplitLast(path);
        name = split.name;
        volume.Walk().FindOutside(split.parent, 0, [this](Result result)
            {
                ParentFound(result);
            });
    }

    void FatLite::ParentFound(Result result)
    {
        const auto& walk = volume.Walk();

        if (result == Result::success && !walk.FoundDirectory())
            result = Result::notADirectory;
        else if (result == Result::success && !walk.FoundRoot() && !volume.Geometry().IsCluster(walk.FoundCluster()))
            result = Result::corrupted;
        else if (result == Result::success)
            result = ValidateFatName(name);

        if (result != Result::success)
            Found(result);
        else
        {
            parent = walk.FoundCluster();
            scan.Scan(parent, name, 1, std::nullopt, std::nullopt, [this](Result result)
                {
                    Scanned(result);
                });
        }
    }

    void FatLite::Scanned(Result result)
    {
        if (result != Result::success)
            Found(result);
        else if (scan.Found())
            OpenExisting(*scan.Found());
        else if (mode == OpenMode::read)
            Found(Result::notFound);
        else
            Create();
    }

    void FatLite::OpenExisting(const FatScannedEntry& found)
    {
        auto result = IsDirectory(found) ? Result::isADirectory : Verify(found);

        if (result == Result::success)
            opening = AllocateFile();

        if (opening == nullptr)
        {
            Found(result == Result::success ? Result::tooManyOpenFiles : result);
            return;
        }

        opening->Open(found, mode);
        pathClaimer.Release();

        if (mode == OpenMode::write && (found.entry.size != 0 || found.entry.firstCluster != 0))
            opening->Truncate([this](Result result)
                {
                    Opened(result);
                });
        else
            OpenDone(Result::success);
    }

    Result FatLite::Verify(const FatScannedEntry& found)
    {
        if (mode != OpenMode::read && (found.entry.attributes & FatAttributes::readOnly) != 0)
            return Result::readOnly;

        if (found.entry.size != 0 && !volume.Geometry().IsCluster(found.entry.firstCluster))
            return Result::corrupted;

        for (const auto& file : volume.OpenFiles())
            if (file.Conflicts(found.shortEntry, mode))
                return Result::busy;

        return Result::success;
    }

    void FatLite::Create()
    {
        auto shortName = ExactShortName(name);
        opening = AllocateFile();

        if (opening == nullptr)
            Found(Result::tooManyOpenFiles);
        else if (!shortName)
            Found(Result::unsupported);
        else if (!scan.Slots())
            Found(Result::noSpace);
        else
        {
            created = FatScannedEntry{};
            created.entry.name = *shortName;
            created.entry.attributes = FatAttributes::archive;
            created.entry.createDate = fatEpoch.date;
            created.entry.accessDate = fatEpoch.date;
            created.entry.writeDate = fatEpoch.date;
            created.slots = 1;

            if (scan.Slots()->extensible)
                GrowDirectory(scan.Slots()->first.cluster);
            else
                WriteEntry(scan.Slots()->first);
        }
    }

    void FatLite::GrowDirectory(uint32_t lastCluster)
    {
        parent = lastCluster;
        volume.Allocator().Take([this](Result result, uint32_t cluster)
            {
                grown = cluster;
                zeroed = 0;

                if (result != Result::success)
                    Found(result);
                else
                    ZeroNext();
            });
    }

    void FatLite::ZeroNext()
    {
        if (zeroed != volume.Geometry().sectorsPerCluster)
            volume.Window().Overwrite(volume.Geometry().SectorOfCluster(grown) + zeroed++, dataRank, [this](Result result, infra::ByteRange)
                {
                    if (result != Result::success)
                        Found(result);
                    else
                        ZeroNext();
                });
        else
            volume.TableWriter().Write(parent, grown, [this](Result result)
                {
                    if (result != Result::success)
                        Found(result);
                    else
                        WriteEntry(FatEntryPosition{ grown, 0 });
                });
    }

    void FatLite::WriteEntry(FatEntryPosition position)
    {
        created.first = position;
        created.shortEntry = position;
        volume.Window().Modify(volume.Reader().SectorOf(position), entryRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success)
                {
                    EncodeShortEntry(created.entry, infra::Head(infra::DiscardHead(contents, created.shortEntry.index % (contents.size() / directoryEntryBytes) * directoryEntryBytes), directoryEntryBytes));
                    opening->Open(created, mode);
                    pathClaimer.Release();
                    OpenDone(Result::success);
                }
                else
                    Found(result);
            });
    }

    void FatLite::Opened(Result result)
    {
        if (result != Result::success)
            opening->Detach();

        OpenDone(result);
    }

    void FatLite::OpenDone(Result result)
    {
        auto file = std::exchange(opening, nullptr);
        onOpen(result, result == Result::success ? file : nullptr);
    }

    void FatLite::BeginMount()
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

    void FatLite::Mounted(Result result)
    {
        lifecycleClaimer.Release();
        lifecycle.Dispatch(FatMountDone{ result });
    }

    void FatLite::BeginUnmount()
    {
        lifecycleClaimer.Claim([this]
            {
                CommitNext();
            });
    }

    void FatLite::CommitNext()
    {
        for (auto& file : volume.OpenFiles())
            if (file.Uncommitted())
            {
                file.Commit([this](Result result)
                    {
                        if (result != Result::success)
                            UnmountDone(result);
                        else
                            CommitNext();
                    });
                return;
            }

        WriteFsInfo();
    }

    void FatLite::WriteFsInfo()
    {
        const auto& geometry = volume.Geometry();

        if (geometry.fsInfoSector.has_value() && volume.Allocator().TableChanged())
            window.Overwrite(*geometry.fsInfoSector, tableRank, [this](Result result, infra::ByteRange contents)
                {
                    if (result != Result::success)
                        UnmountDone(result);
                    else
                    {
                        SerializeFsInfo(FsInfo{ std::nullopt, volume.Allocator().Cursor() }, contents);
                        window.Flush([this](Result result)
                            {
                                UnmountDone(result);
                            });
                    }
                });
        else
            window.Flush([this](Result result)
                {
                    UnmountDone(result);
                });
    }

    void FatLite::UnmountDone(Result result)
    {
        volume.Report(result);
        lifecycleClaimer.Release();
        lifecycle.Dispatch(FatUnmountDone{ result });
    }

    void FatLite::Entered(Result result)
    {
        if (onMountDone)
            volume.Defer(std::exchange(onMountDone, nullptr), result);
        if (onUnmountDone)
            volume.Defer(std::exchange(onUnmountDone, nullptr), result);
    }
}
