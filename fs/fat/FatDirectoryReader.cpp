#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatDirectoryEntry.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatDirectoryVisitor::FatDirectoryVisitor() = default;

    FatDirectoryReader::FatDirectoryReader(SectorCache& cache, const FatGeometry& geometry, FatEntryAssembler& assembler)
        : cache(cache)
        , geometry(geometry)
        , assembler(assembler)
        , table(cache, geometry)
    {}

    FatDirectoryReader::~FatDirectoryReader() = default;

    FatDirectoryCursor FatDirectoryReader::Start(uint32_t firstCluster) const
    {
        if (firstCluster == 0 && geometry.type == FatType::fat32)
            return FatDirectoryCursor{ geometry.rootCluster };

        return FatDirectoryCursor{ firstCluster };
    }

    void FatDirectoryReader::Read(FatDirectoryCursor& cursor, FatDirectoryVisitor& visitor, const infra::Function<void(Result result)>& onDone)
    {
        this->cursor = &cursor;
        this->visitor = &visitor;
        this->onDone = onDone;
        assembler.Reset();

        if (cursor.done || AtEndOfSector())
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    Resume();
                });
        else
            ReadSector();
    }

    uint32_t FatDirectoryReader::SectorOf(FatEntryPosition position) const
    {
        auto first = position.cluster == 0 ? geometry.rootSector : geometry.SectorOfCluster(position.cluster);

        return first + position.index / EntriesPerSector();
    }

    uint32_t FatDirectoryReader::Capacity(uint32_t cluster) const
    {
        return cluster == 0 ? geometry.rootSectors * EntriesPerSector() : EntriesPerCluster();
    }

    void FatDirectoryReader::Resume()
    {
        if (cursor->done)
            Done(Result::success);
        else
            NextSector();
    }

    void FatDirectoryReader::ReadSector()
    {
        cache.Read(SectorOf(FatEntryPosition{ cursor->cluster, cursor->index }), [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatDirectoryReader::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        do
        {
            if (cursor->entriesVisited++ == maxEntries)
            {
                Done(Result::corrupted);
                return;
            }

            auto entry = infra::Head(infra::DiscardHead(contents, (cursor->index % EntriesPerSector()) * directoryEntryBytes), directoryEntryBytes);

            if (!FeedEntry(entry))
                return;
        } while (cursor->index % EntriesPerSector() != 0);

        NextSector();
    }

    bool FatDirectoryReader::FeedEntry(infra::ConstByteRange entry)
    {
        FatEntryPosition position{ cursor->cluster, cursor->index++ };

        switch (assembler.Feed(entry, position))
        {
            case FatEntryAssembler::Outcome::end:
                visitor->FreeSlot(position, true);
                cursor->done = true;
                Done(Result::success);
                return false;
            case FatEntryAssembler::Outcome::entry:
                if (visitor->Visit(assembler.Entry()))
                    return true;

                Done(Result::success);
                return false;
            default:
                if (KindOf(entry) == EntryKind::free)
                    visitor->FreeSlot(position, false);

                return true;
        }
    }

    void FatDirectoryReader::NextSector()
    {
        if (IsFixedRoot())
        {
            cursor->done = cursor->index == Capacity(0);

            if (cursor->done)
                Done(Result::success);
            else
                ReadSector();
        }
        else if (cursor->index != EntriesPerCluster())
            ReadSector();
        else if (++cursor->clustersVisited == geometry.clusterCount)
            Done(Result::corrupted);
        else
            table.Next(cursor->cluster, [this](Result result, std::optional<uint32_t> next)
                {
                    OnNextCluster(result, next);
                });
    }

    void FatDirectoryReader::OnNextCluster(Result result, std::optional<uint32_t> next)
    {
        if (result != Result::success)
            Done(result);
        else if (!next)
        {
            cursor->done = true;
            Done(Result::success);
        }
        else
        {
            cursor->cluster = *next;
            cursor->index = 0;
            ReadSector();
        }
    }

    void FatDirectoryReader::Done(Result result)
    {
        onDone(result);
    }

    uint32_t FatDirectoryReader::EntriesPerSector() const
    {
        return cache.SectorSize() / directoryEntryBytes;
    }

    uint32_t FatDirectoryReader::EntriesPerCluster() const
    {
        return EntriesPerSector() * geometry.sectorsPerCluster;
    }

    bool FatDirectoryReader::AtEndOfSector() const
    {
        return cursor->index != 0 && cursor->index % EntriesPerSector() == 0;
    }

    bool FatDirectoryReader::IsFixedRoot() const
    {
        return cursor->cluster == 0;
    }
}
