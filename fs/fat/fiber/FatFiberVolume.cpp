#include "fs/fat/fiber/FatFiberVolume.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatName.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include "fs/fat/FatTable.hpp"

namespace fs
{
    FatFiberVolume::FatFiberVolume(hal::BlockDevice& device, fiber::Runner& runner, FatFiberWindow& window, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const infra::Function<void()>& onMediaRemoved)
        : device(device)
        , runner(runner)
        , window(window)
        , deferred(deferred)
        , onMediaRemoved(onMediaRemoved)
        , assembler(nameBuffer)
    {}

    infra::ClaimableResource& FatFiberVolume::Queue()
    {
        return queue;
    }

    fiber::Runner& FatFiberVolume::Fiber() const
    {
        return runner;
    }

    FatFiberWindow& FatFiberVolume::Window() const
    {
        return window;
    }

    const FatGeometry& FatFiberVolume::Geometry() const
    {
        return geometry;
    }

    const FatScannedEntry& FatFiberVolume::Entry() const
    {
        return assembler.Entry();
    }

    bool FatFiberVolume::FoundRoot() const
    {
        return !found.has_value();
    }

    const FatScannedEntry& FatFiberVolume::Found() const
    {
        return *found;
    }

    bool FatFiberVolume::FoundDirectory() const
    {
        return FoundRoot() || IsDirectory(*found);
    }

    uint32_t FatFiberVolume::FoundCluster() const
    {
        return FoundRoot() ? 0 : found->entry.firstCluster;
    }

    FatDirectoryCursor FatFiberVolume::Start(uint32_t firstCluster) const
    {
        if (firstCluster == 0 && geometry.type == FatType::fat32)
            return FatDirectoryCursor{ geometry.rootCluster };

        return FatDirectoryCursor{ firstCluster };
    }

    Result FatFiberVolume::Mount()
    {
        if (device.BlockSize() != window.SectorSize())
            return Result::unsupported;

        if (auto result = window.Read(0); result != Result::success)
            return result;

        if (ParseBootSector(window.Contents()))
            return UseGeometry(window.Contents(), 0, device.NumberOfBlocks());

        if (IsProtectiveMbr(window.Contents()))
            return Result::unsupported;

        auto partition = FirstFatPartition(window.Contents());

        if (!partition || partition->firstSector >= device.NumberOfBlocks() || partition->sectors > device.NumberOfBlocks() - partition->firstSector)
            return Result::corrupted;

        auto start = partition->firstSector;
        auto sectors = partition->sectors;

        if (auto result = window.Read(start); result != Result::success)
            return result;

        if (ParseBootSector(window.Contents()))
            return UseGeometry(window.Contents(), start, sectors);

        return Result::corrupted;
    }

    Result FatFiberVolume::NextCluster(uint32_t cluster)
    {
        auto location = LocateEntry(geometry.type, cluster, window.SectorSize());
        auto sector = geometry.ActiveFatSector() + location.sector;
        nextCluster = std::nullopt;
        auto result = window.Read(sector);

        if (result == Result::success)
        {
            uint32_t value = 0;

            if (location.offset + 1 != window.SectorSize() || geometry.type != FatType::fat12)
                value = ReadEntry(geometry.type, cluster, window.Contents(), infra::ConstByteRange());
            else
            {
                auto lowByte = window.Contents()[location.offset];
                result = window.Read(sector + 1);
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

        return result;
    }

    FatEntryStep FatFiberVolume::NextEntry(FatDirectoryCursor& cursor)
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
                        return FatEntryStep{};
                    }
                }
                else if (cursor.index == EntriesPerCluster())
                {
                    if (++cursor.clustersVisited == geometry.clusterCount)
                        return FatEntryStep{ Result::corrupted };

                    if (auto result = NextCluster(cursor.cluster); result != Result::success)
                        return FatEntryStep{ result };

                    if (!nextCluster)
                    {
                        cursor.done = true;
                        return FatEntryStep{};
                    }

                    cursor.cluster = *nextCluster;
                    cursor.index = 0;
                }
            }

            if (auto result = window.Read(SectorOf(cursor)); result != Result::success)
                return FatEntryStep{ result };

            do
            {
                if (cursor.entriesVisited++ == FatDirectoryReader::maxEntries)
                    return FatEntryStep{ Result::corrupted };

                auto entry = infra::Head(infra::DiscardHead(window.Contents(), (cursor.index % EntriesPerSector()) * directoryEntryBytes), directoryEntryBytes);
                FatEntryPosition position{ cursor.cluster, cursor.index++ };

                switch (assembler.Feed(entry, position))
                {
                    case FatEntryAssembler::Outcome::end:
                        cursor.done = true;
                        return FatEntryStep{};
                    case FatEntryAssembler::Outcome::entry:
                        return FatEntryStep{ Result::success, true };
                    default:
                        break;
                }
            } while (cursor.index % EntriesPerSector() != 0);
        }

        return FatEntryStep{};
    }

    Result FatFiberVolume::Find(infra::BoundedConstString path)
    {
        PathComponents components(path);
        auto cursor = Start(0);
        found = std::nullopt;

        while (auto component = components.Next())
        {
            if (auto validation = ValidateFatName(*component); validation != Result::success)
                return validation;

            found = std::nullopt;

            while (!found)
            {
                auto step = NextEntry(cursor);

                if (step.result != Result::success)
                    return step.result;

                if (!step.found)
                    return Result::notFound;

                if (NameMatches(assembler.Entry(), *component))
                    found = assembler.Entry();
            }

            if (components.Done())
                return Result::success;

            if (!FoundDirectory())
                return Result::notADirectory;

            if (!geometry.IsCluster(FoundCluster()))
                return Result::corrupted;

            cursor = Start(FoundCluster());
        }

        return Result::success;
    }

    std::optional<uint32_t> FatFiberVolume::NextClusterFound() const
    {
        return nextCluster;
    }

    uint32_t FatFiberVolume::Generation() const
    {
        return generation;
    }

    bool FatFiberVolume::Idle() const
    {
        return !queue.IsClaimed() && !queue.ClaimsPending();
    }

    bool FatFiberVolume::Available() const
    {
        return mounted;
    }

    bool FatFiberVolume::Available(uint32_t handleGeneration) const
    {
        return mounted && handleGeneration == generation;
    }

    Result FatFiberVolume::Unavailable() const
    {
        return unavailable;
    }

    void FatFiberVolume::MarkMounted()
    {
        ++generation;
        mounted = true;
    }

    void FatFiberVolume::MarkUnmounted(Result reason)
    {
        mounted = false;
        unavailable = reason == Result::mediaRemoved ? Result::mediaRemoved : Result::notMounted;
    }

    void FatFiberVolume::Report(Result result) const
    {
        if (result == Result::mediaRemoved)
            onMediaRemoved();
    }

    Result FatFiberVolume::UseGeometry(infra::ConstByteRange bootSector, uint32_t start, uint32_t sectors)
    {
        auto layout = GeometryOf(*ParseBootSector(bootSector), start, sectors, device.BlockSize());
        geometry = layout.geometry;
        return layout.result;
    }

    uint32_t FatFiberVolume::EntriesPerSector() const
    {
        return window.SectorSize() / directoryEntryBytes;
    }

    uint32_t FatFiberVolume::EntriesPerCluster() const
    {
        return EntriesPerSector() * geometry.sectorsPerCluster;
    }

    uint32_t FatFiberVolume::SectorOf(const FatDirectoryCursor& cursor) const
    {
        auto first = cursor.cluster == 0 ? geometry.rootSector : geometry.SectorOfCluster(cursor.cluster);

        return first + cursor.index / EntriesPerSector();
    }
}
