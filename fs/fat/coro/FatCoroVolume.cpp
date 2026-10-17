#include "fs/fat/coro/FatCoroVolume.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatName.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatCoroVolume::FatCoroVolume(hal::BlockDevice& device, FatCoroWindow& window, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved)
        : device(device)
        , window(window)
        , deferred(deferred)
        , onMediaRemoved(onMediaRemoved)
        , assembler(nameBuffer)
    {}

    infra::ClaimableResource& FatCoroVolume::Queue()
    {
        return queue;
    }

    hal::BlockDevice& FatCoroVolume::Device() const
    {
        return device;
    }

    FatCoroWindow& FatCoroVolume::Window() const
    {
        return window;
    }

    const FatGeometry& FatCoroVolume::Geometry() const
    {
        return geometry;
    }

    const FatScannedEntry& FatCoroVolume::Entry() const
    {
        return assembler.Entry();
    }

    bool FatCoroVolume::FoundRoot() const
    {
        return !found.has_value();
    }

    const FatScannedEntry& FatCoroVolume::Found() const
    {
        return *found;
    }

    bool FatCoroVolume::FoundDirectory() const
    {
        return FoundRoot() || IsDirectory(*found);
    }

    uint32_t FatCoroVolume::FoundCluster() const
    {
        return FoundRoot() ? 0 : found->entry.firstCluster;
    }

    FatDirectoryCursor FatCoroVolume::Start(uint32_t firstCluster) const
    {
        if (firstCluster == 0 && geometry.type == FatType::fat32)
            return FatDirectoryCursor{ geometry.rootCluster };

        return FatDirectoryCursor{ firstCluster };
    }

    coro::Task<Result> FatCoroVolume::Mount(coro::Frames&)
    {
        if (device.BlockSize() != window.SectorSize())
            co_return Result::unsupported;

        if (auto result = co_await window.Read(0); result != Result::success)
            co_return result;

        if (ParseBootSector(window.Contents()))
            co_return UseGeometry(window.Contents(), 0, device.NumberOfBlocks());

        if (IsProtectiveMbr(window.Contents()))
            co_return Result::unsupported;

        auto partition = FirstFatPartition(window.Contents());

        if (!partition || partition->firstSector >= device.NumberOfBlocks() || partition->sectors > device.NumberOfBlocks() - partition->firstSector)
            co_return Result::corrupted;

        auto start = partition->firstSector;
        auto sectors = partition->sectors;

        if (auto result = co_await window.Read(start); result != Result::success)
            co_return result;

        if (ParseBootSector(window.Contents()))
            co_return UseGeometry(window.Contents(), start, sectors);

        co_return Result::corrupted;
    }

    coro::Task<Result> FatCoroVolume::NextCluster(coro::Frames&, uint32_t cluster)
    {
        auto location = LocateEntry(geometry.type, cluster, window.SectorSize());
        auto sector = geometry.ActiveFatSector() + location.sector;
        nextCluster = std::nullopt;
        auto result = co_await window.Read(sector);

        if (result == Result::success)
        {
            uint32_t value = 0;

            if (location.offset + 1 != window.SectorSize() || geometry.type != FatType::fat12)
                value = ReadEntry(geometry.type, cluster, window.Contents(), infra::ConstByteRange());
            else
            {
                auto lowByte = window.Contents()[location.offset];
                result = co_await window.Read(sector + 1);
                value = DecodeFat12(cluster, static_cast<uint16_t>(lowByte | (window.Contents()[0] << 8)));
            }

            if (result == Result::success && !IsEndOfChain(geometry.type, value))
            {
                if (geometry.IsCluster(value))
                    nextCluster = value;
                else
                    result = Result::corrupted;
            }
        }

        co_return result;
    }

    coro::Task<FatEntryStep> FatCoroVolume::NextEntry(coro::Frames& frames, FatDirectoryCursor& cursor)
    {
        assembler.Reset();

        while (!cursor.done)
        {
            if (cursor.index != 0 && cursor.index % EntriesPerSector() == 0)
            {
                if (cursor.cluster == 0)
                {
                    if (cursor.index == geometry.rootSectors * EntriesPerSector())
                    {
                        cursor.done = true;
                        co_return FatEntryStep{};
                    }
                }
                else if (cursor.index == EntriesPerCluster())
                {
                    if (++cursor.clustersVisited == geometry.clusterCount)
                        co_return FatEntryStep{ Result::corrupted };

                    if (auto result = co_await NextCluster(frames, cursor.cluster); result != Result::success)
                        co_return FatEntryStep{ result };

                    if (!nextCluster)
                    {
                        cursor.done = true;
                        co_return FatEntryStep{};
                    }

                    cursor.cluster = *nextCluster;
                    cursor.index = 0;
                }
            }

            if (auto result = co_await window.Read(SectorOf(cursor)); result != Result::success)
                co_return FatEntryStep{ result };

            do
            {
                if (cursor.entriesVisited++ == FatDirectoryReader::maxEntries)
                    co_return FatEntryStep{ Result::corrupted };

                auto entry = infra::Head(infra::DiscardHead(window.Contents(), (cursor.index % EntriesPerSector()) * directoryEntryBytes), directoryEntryBytes);
                FatEntryPosition position{ cursor.cluster, cursor.index++ };

                switch (assembler.Feed(entry, position))
                {
                    case FatEntryAssembler::Outcome::end:
                        cursor.done = true;
                        co_return FatEntryStep{};
                    case FatEntryAssembler::Outcome::entry:
                        co_return FatEntryStep{ Result::success, true };
                    default:
                        break;
                }
            } while (cursor.index % EntriesPerSector() != 0);
        }

        co_return FatEntryStep{};
    }

    coro::Task<Result> FatCoroVolume::Find(coro::Frames& frames, infra::BoundedConstString path)
    {
        PathComponents components(path);
        auto cursor = Start(0);
        found = std::nullopt;

        while (auto component = components.Next())
        {
            if (auto validation = ValidateFatName(*component); validation != Result::success)
                co_return validation;

            found = std::nullopt;

            while (!found)
            {
                auto step = co_await NextEntry(frames, cursor);

                if (step.result != Result::success)
                    co_return step.result;

                if (!step.found)
                    co_return Result::notFound;

                if (NameMatches(assembler.Entry(), *component))
                    found = assembler.Entry();
            }

            if (components.Done())
                co_return Result::success;

            if (!FoundDirectory())
                co_return Result::notADirectory;

            if (!geometry.IsCluster(FoundCluster()))
                co_return Result::corrupted;

            cursor = Start(FoundCluster());
        }

        co_return Result::success;
    }

    std::optional<uint32_t> FatCoroVolume::NextClusterFound() const
    {
        return nextCluster;
    }

    uint32_t FatCoroVolume::Generation() const
    {
        return generation;
    }

    bool FatCoroVolume::Idle() const
    {
        return !queue.IsClaimed() && !queue.ClaimsPending();
    }

    bool FatCoroVolume::Available() const
    {
        return mounted;
    }

    bool FatCoroVolume::Available(uint32_t handleGeneration) const
    {
        return mounted && handleGeneration == generation;
    }

    Result FatCoroVolume::Unavailable() const
    {
        return unavailable;
    }

    void FatCoroVolume::MarkMounted()
    {
        ++generation;
        mounted = true;
    }

    void FatCoroVolume::MarkUnmounted(Result reason)
    {
        mounted = false;
        unavailable = reason == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }

    void FatCoroVolume::Report(Result result) const
    {
        if (result == Result::mediaRemoved)
            onMediaRemoved();
    }

    Result FatCoroVolume::UseGeometry(infra::ConstByteRange bootSector, uint32_t start, uint32_t sectors)
    {
        auto layout = GeometryOf(*ParseBootSector(bootSector), start, sectors, device.BlockSize());
        geometry = layout.geometry;
        return layout.result;
    }

    uint32_t FatCoroVolume::EntriesPerSector() const
    {
        return window.SectorSize() / directoryEntryBytes;
    }

    uint32_t FatCoroVolume::EntriesPerCluster() const
    {
        return EntriesPerSector() * geometry.sectorsPerCluster;
    }

    uint32_t FatCoroVolume::SectorOf(const FatDirectoryCursor& cursor) const
    {
        auto first = cursor.cluster == 0 ? geometry.rootSector : geometry.SectorOfCluster(cursor.cluster);

        return first + cursor.index / EntriesPerSector();
    }

}
