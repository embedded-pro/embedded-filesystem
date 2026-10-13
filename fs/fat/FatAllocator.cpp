#include "fs/fat/FatAllocator.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatAllocator::FatAllocator(SectorCache& cache, FatVolumeState& volume, FatTableWriter& writer)
        : cache(cache)
        , volume(volume)
        , writer(writer)
    {}

    FatAllocator::~FatAllocator() = default;

    void FatAllocator::Reset()
    {
        cursor = 2;

        if (volume.cleanAtMount == true && volume.fsInfo.has_value() && volume.fsInfo->nextFree.has_value() && IsCluster(*volume.fsInfo->nextFree))
            cursor = *volume.fsInfo->nextFree;
    }

    uint32_t FatAllocator::Cursor() const
    {
        return cursor;
    }

    void FatAllocator::Grow(std::optional<uint32_t> last, uint32_t clusters, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone)
    {
        really_assert(clusters != 0);

        this->last = last;
        this->onDone = onDone;
        wanted = clusters;
        allocated = 0;
        first = 0;
        barrier = false;
        Next();
    }

    void FatAllocator::Next()
    {
        if (allocated == wanted)
        {
            Link(Result::success);
            return;
        }

        auto preferred = Preferred();
        preferredOnly = preferred != 0;
        candidate = preferredOnly ? preferred : cursor;
        examined = 0;
        ReadCandidateSector();
    }

    void FatAllocator::ReadCandidateSector()
    {
        cache.Read(writer.SectorOf(candidate), [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatAllocator::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            Link(result);
            return;
        }

        if (auto found = FreeInSector(contents))
            Take(*found);
        else if (preferredOnly)
        {
            preferredOnly = false;
            candidate = cursor;
            examined = 0;
            ReadCandidateSector();
        }
        else if (examined >= volume.geometry.clusterCount)
            Link(Result::noSpace);
        else
            ReadCandidateSector();
    }

    std::optional<uint32_t> FatAllocator::FreeInSector(infra::ConstByteRange contents)
    {
        const auto& geometry = volume.geometry;
        auto sector = writer.SectorOf(candidate);

        while (writer.SectorOf(candidate) == sector && examined < geometry.clusterCount)
        {
            auto cluster = candidate;
            auto location = LocateEntry(geometry.type, cluster, cache.SectorSize());
            candidate = After(cluster) != 0 ? After(cluster) : 2;
            ++examined;

            if (!location.straddles && ReadEntry(geometry.type, cluster, contents, infra::ConstByteRange()) == 0)
                return cluster;

            if (preferredOnly)
                return std::nullopt;
        }

        return std::nullopt;
    }

    void FatAllocator::Take(uint32_t cluster)
    {
        writer.Write(cluster, EndOfChain(volume.geometry.type), [this, cluster](Result result)
            {
                if (result == Result::success && allocated != 0)
                    writer.Write(previous, cluster, [this, cluster](Result result)
                        {
                            Taken(result, cluster);
                        });
                else
                    Taken(result, cluster);
            });
    }

    void FatAllocator::Taken(Result result, uint32_t cluster)
    {
        if (result != Result::success)
        {
            Link(result);
            return;
        }

        if (allocated == 0)
            first = cluster;

        barrier = barrier || (last.has_value() && writer.SectorOf(cluster) != writer.SectorOf(*last));
        previous = cluster;
        ++allocated;
        cursor = After(cluster) != 0 ? After(cluster) : 2;

        if (volume.freeClusters.has_value())
            --*volume.freeClusters;

        Next();
    }

    void FatAllocator::Link(Result result)
    {
        if (result != Result::success && result != Result::noSpace)
            Done(result);
        else if (allocated == 0)
            Done(Result::noSpace);
        else
            LinkAllocated(result);
    }

    void FatAllocator::LinkAllocated(Result result)
    {
        if (!last.has_value())
            Done(result);
        else if (barrier)
            cache.Flush([this, result](Result flushed)
                {
                    if (flushed != Result::success)
                        Done(flushed);
                    else
                        LinkLast(result);
                });
        else
            LinkLast(result);
    }

    void FatAllocator::LinkLast(Result result)
    {
        writer.Write(*last, first, [this, result](Result written)
            {
                Done(written != Result::success ? written : result);
            });
    }

    void FatAllocator::Done(Result result)
    {
        onDone(result, first, allocated);
    }

    uint32_t FatAllocator::Preferred() const
    {
        if (allocated != 0)
            return After(previous);

        if (last.has_value())
            return After(*last);

        return 0;
    }

    bool FatAllocator::IsCluster(uint32_t cluster) const
    {
        return volume.geometry.IsCluster(cluster);
    }

    uint32_t FatAllocator::After(uint32_t cluster) const
    {
        return IsCluster(cluster + 1) ? cluster + 1 : 0;
    }
}
