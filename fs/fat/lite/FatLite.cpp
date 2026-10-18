#include "fs/fat/lite/FatLite.hpp"
#include "fs/fat/FatFsInfo.hpp"
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
                  if (lifecycle.RemoveMedia())
                      volume.EnterUnmounted(Result::mediaRemoved);
              })
        , mount(device, window, volume.Geometry())
        , search(volume.Reader(), volume.Geometry())
        , limits(limits)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
        , resume([this](Result result)
              {
                  Resume(result);
              })
    {}

    FatLite::~FatLite() = default;

    void FatLite::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (!lifecycle.StartMount())
            volume.Defer(onDone, Result::busy);
        else
            BeginChange(onDone);
    }

    void FatLite::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.StartUnmount())
            BeginChange(onDone);
        else
            volume.Defer(onDone, lifecycle.Changing() ? Result::busy : lifecycle.Unavailable());
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

    Result FatLite::Unsupported() const
    {
        return lifecycle.IsMounted() ? Result::unsupported : lifecycle.Unavailable();
    }

    Result FatLite::Refusal(infra::BoundedConstString path) const
    {
        if (!lifecycle.IsMounted())
            return lifecycle.Unavailable();

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
            Found(lifecycle.Unavailable());
        else
            search.Find(path, Then(onOpen ? Step::searched : Step::found));
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
        if (result != Result::success)
            onStat(result, EntryInfo{});
        else if (search.FoundRoot())
            onStat(result, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            onStat(result, InfoOf(search.Found()));
    }

    void FatLite::OpenDirectoryFound(Result result)
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

    void FatLite::Searched(Result result)
    {
        if (result == Result::notFound && search.LastMissing() && mode != OpenMode::read)
            Create();
        else if (result != Result::success)
            Found(result);
        else if (search.FoundDirectory())
            Found(Result::isADirectory);
        else
            OpenExisting(search.Found());
    }

    void FatLite::OpenExisting(const FatScannedEntry& found)
    {
        auto result = Verify(found);

        if (result == Result::success)
            opening = AllocateFile();

        if (opening == nullptr)
        {
            Found(result == Result::success ? Result::tooManyOpenFiles : result);
            return;
        }

        opening->Open(found.shortEntry, found.entry, mode);
        pathClaimer.Release();

        if (mode == OpenMode::write && (found.entry.size != 0 || found.entry.firstCluster != 0))
            opening->Truncate(Then(Step::opened));
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
        auto shortName = ExactShortName(search.Component());
        opening = AllocateFile();

        if (opening == nullptr)
            Found(Result::tooManyOpenFiles);
        else if (!shortName)
            Found(Result::unsupported);
        else if (!search.Free())
            Found(Result::noSpace);
        else
        {
            createdName = *shortName;

            if (search.Extensible())
                GrowDirectory(search.Free()->cluster);
            else
                WriteEntry(*search.Free());
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
            volume.TableWriter().Write(parent, grown, Then(Step::linked));
    }

    void FatLite::Linked(Result result)
    {
        if (result != Result::success)
            Found(result);
        else
            WriteEntry(FatEntryPosition{ grown, 0 });
    }

    void FatLite::WriteEntry(FatEntryPosition position)
    {
        createdAt = position;
        volume.Window().Modify(volume.Reader().SectorOf(position), entryRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success)
                {
                    ShortEntry entry;
                    entry.name = createdName;
                    entry.attributes = FatAttributes::archive;
                    entry.createDate = fatEpoch.date;
                    entry.accessDate = fatEpoch.date;
                    entry.writeDate = fatEpoch.date;
                    EncodeShortEntry(entry, infra::Head(infra::DiscardHead(contents, createdAt.index % (contents.size() / directoryEntryBytes) * directoryEntryBytes), directoryEntryBytes));
                    opening->Open(createdAt, entry, mode);
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

    const infra::Function<void(Result result)>& FatLite::Then(Step next)
    {
        step = next;
        return resume;
    }

    void FatLite::Resume(Result result)
    {
        switch (step)
        {
            case Step::found:
                Found(result);
                break;
            case Step::searched:
                Searched(result);
                break;
            case Step::linked:
                Linked(result);
                break;
            case Step::opened:
                Opened(result);
                break;
            case Step::mounted:
                Mounted(result);
                break;
            case Step::committed:
                Committed(result);
                break;
            default:
                UnmountDone(result);
                break;
        }
    }

    void FatLite::BeginChange(const infra::Function<void(Result result)>& onDone)
    {
        onChanged = onDone;
        lifecycleClaimer.Claim([this]
            {
                if (lifecycle.Mounting())
                {
                    window.Forget();
                    mount.Start(Then(Step::mounted));
                }
                else
                    CommitNext();
            });
    }

    void FatLite::Mounted(Result result)
    {
        lifecycleClaimer.Release();
        lifecycle.Mounted(result);

        if (result == Result::success)
            volume.EnterMounted();
        else
            volume.EnterUnmounted(result);

        Entered(result);
    }

    void FatLite::CommitNext()
    {
        for (auto& file : volume.OpenFiles())
            if (file.Uncommitted())
            {
                file.Commit(Then(Step::committed));
                return;
            }

        WriteFsInfo();
    }

    void FatLite::Committed(Result result)
    {
        if (result != Result::success)
            UnmountDone(result);
        else
            CommitNext();
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
                        window.Flush(Then(Step::unmounted));
                    }
                });
        else
            window.Flush(Then(Step::unmounted));
    }

    void FatLite::UnmountDone(Result result)
    {
        volume.Report(result);
        lifecycleClaimer.Release();
        lifecycle.Unmounted();
        volume.EnterUnmounted(Result::notMounted);
        Entered(result);
    }

    void FatLite::Entered(Result result)
    {
        volume.Defer(std::exchange(onChanged, nullptr), result);
    }
}
