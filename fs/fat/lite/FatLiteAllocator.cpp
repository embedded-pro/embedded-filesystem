#include "fs/fat/lite/FatLiteAllocator.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/lite/FatLiteVolume.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t firstCluster = 2;
    }

    FatLiteAllocator::FatLiteAllocator(FatLiteVolume& volume)
        : volume(volume)
    {}

    FatLiteAllocator::~FatLiteAllocator() = default;

    void FatLiteAllocator::Reset()
    {
        cursor = firstCluster;
        tableChanged = false;
    }

    void FatLiteAllocator::Take(const infra::Function<void(Result result, uint32_t cluster)>& onDone)
    {
        onTaken = onDone;
        candidate = cursor;
        searched = 0;
        ReadSector();
    }

    void FatLiteAllocator::Free(uint32_t first, const infra::Function<void(Result result)>& onDone)
    {
        onFreed = onDone;
        candidate = first;
        FreeNext();
    }

    uint32_t FatLiteAllocator::Cursor() const
    {
        return cursor;
    }

    bool FatLiteAllocator::TableChanged() const
    {
        return tableChanged;
    }

    void FatLiteAllocator::ReadSector()
    {
        volume.Window().Read(SectorOf(candidate), [this](Result result, infra::ConstByteRange contents)
            {
                Search(result, contents);
            });
    }

    void FatLiteAllocator::Search(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            onTaken(result, 0);
            return;
        }

        const auto& geometry = volume.Geometry();
        auto sector = SectorOf(candidate);

        for (; searched != geometry.clusterCount; Advance())
        {
            if (SectorOf(candidate) != sector)
            {
                ReadSector();
                return;
            }

            if (!LocateEntry(geometry.type, candidate, volume.Window().SectorSize()).straddles && ReadEntry(geometry.type, candidate, contents, infra::ConstByteRange()) == 0)
            {
                // The window holds the sector of the entry, so writing it cannot fail
                WriteCandidate(EndOfChain(geometry.type));
                return;
            }
        }

        onTaken(Result::noSpace, 0);
    }

    void FatLiteAllocator::Advance()
    {
        ++searched;

        if (++candidate == volume.Geometry().clusterCount + firstCluster)
            candidate = firstCluster;
    }

    void FatLiteAllocator::Taken()
    {
        tableChanged = true;
        auto taken = candidate;
        Advance();
        cursor = candidate;
        onTaken(Result::success, taken);
    }

    void FatLiteAllocator::FreeNext()
    {
        volume.Table().Next(candidate, [this](Result result, std::optional<uint32_t> following)
            {
                next = following;

                if (result != Result::success)
                    onFreed(result);
                else
                    WriteCandidate(0);
            });
    }

    void FatLiteAllocator::WriteCandidate(uint32_t value)
    {
        volume.TableWriter().Write(candidate, value, [this](Result result)
            {
                if (onTaken)
                    Taken();
                else
                    Freed(result);
            });
    }

    void FatLiteAllocator::Freed(Result result)
    {
        if (result == Result::success)
            tableChanged = true;

        if (result != Result::success || !next.has_value())
            onFreed(result);
        else
        {
            candidate = *next;
            FreeNext();
        }
    }

    uint32_t FatLiteAllocator::SectorOf(uint32_t cluster) const
    {
        return volume.TableWriter().SectorOf(cluster);
    }
}
