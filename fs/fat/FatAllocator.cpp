#include "fs/fat/FatAllocator.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatAllocator::FatAllocator(SectorCache& cache, FatVolumeState& volume, FatTableWriter& writer, const FatReservations& reservations, FatFreeSpaceScan& freeSpace)
        : cache(cache)
        , volume(volume)
        , writer(writer)
        , reservations(reservations)
        , freeSpace(freeSpace)
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

    uint32_t FatAllocator::Contiguous() const
    {
        return contiguous;
    }

    void FatAllocator::Grow(std::optional<uint32_t> last, uint32_t clusters, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone)
    {
        Start(last, 0, clusters, true, nullptr, FatChain::referenced, onDone);
    }

    void FatAllocator::Grow(std::optional<uint32_t> last, uint32_t clusters, FatReservation& reservation, FatChain chain, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone)
    {
        Start(last, 0, clusters, true, &reservation, chain, onDone);
    }

    void FatAllocator::GrowAt(uint32_t start, uint32_t clusters, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone)
    {
        Start(std::nullopt, start, clusters, true, nullptr, FatChain::referenced, onDone);
    }

    void FatAllocator::Take(uint32_t last, uint32_t clusters, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone)
    {
        Start(last, 0, clusters, false, nullptr, FatChain::referenced, onDone);
    }

    void FatAllocator::Link(const infra::Function<void(Result result)>& onDone)
    {
        really_assert(last.has_value() && allocated != 0);

        onLinked = onDone;
        LinkAfterBarrier();
    }

    void FatAllocator::Start(std::optional<uint32_t> last, uint32_t start, uint32_t clusters, bool link, FatReservation* reservation, FatChain chain, const infra::Function<void(Result result, uint32_t first, uint32_t allocated)>& onDone)
    {
        really_assert(clusters != 0);

        this->last = last;
        this->start = start;
        own = reservation;
        this->chain = chain;
        this->onDone = onDone;
        linkWhenTaken = link;
        wanted = clusters;
        allocated = 0;
        first = 0;
        contiguous = 0;
        barrier = false;
        Next();
    }

    void FatAllocator::Next()
    {
        if (allocated == wanted)
        {
            Finish(Result::success);
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
            Finish(result);
            return;
        }

        if (auto found = FreeInSector(contents); found.has_value())
            Take(*found);
        else if (preferredOnly)
        {
            if (own != nullptr)
                *own = FatReservation{};

            preferredOnly = false;
            candidate = cursor;
            examined = 0;
            ReadCandidateSector();
        }
        else if (examined >= volume.geometry.clusterCount)
            Finish(Result::noSpace);
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

            if (!location.straddles && ReadEntry(geometry.type, cluster, contents, infra::ConstByteRange()) == 0 && Available(cluster))
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
            Finish(result);
            return;
        }

        if (allocated == 0)
            first = cluster;

        if (own != nullptr && own->count != 0 && own->first == cluster)
        {
            ++own->first;
            --own->count;
        }

        if (contiguous == allocated && cluster == first + allocated)
            ++contiguous;

        barrier = barrier || (chain == FatChain::referenced && last.has_value() && writer.SectorOf(cluster) != writer.SectorOf(*last));
        previous = cluster;
        ++allocated;
        volume.tableChanged = true;
        cursor = After(cluster) != 0 ? After(cluster) : 2;
        freeSpace.Allocated(cluster);

        Next();
    }

    void FatAllocator::Finish(Result result)
    {
        if (result != Result::success && result != Result::noSpace)
            Done(result);
        else if (allocated == 0)
            Done(Result::noSpace);
        else
            FinishAllocated(result);
    }

    void FatAllocator::FinishAllocated(Result result)
    {
        if (!last.has_value() || !linkWhenTaken)
            Done(result);
        else
        {
            takeResult = result;
            onLinked = [this](Result linked)
            {
                Done(linked != Result::success ? linked : takeResult);
            };
            LinkAfterBarrier();
        }
    }

    void FatAllocator::LinkAfterBarrier()
    {
        if (barrier)
            cache.Flush([this](Result flushed)
                {
                    if (flushed != Result::success)
                        onLinked(flushed);
                    else
                        LinkLast();
                });
        else
            LinkLast();
    }

    void FatAllocator::LinkLast()
    {
        writer.Write(*last, first, [this](Result written)
            {
                onLinked(written);
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

        if (own != nullptr && own->count != 0)
            return own->first;

        if (last.has_value())
            return After(*last);

        return start;
    }

    bool FatAllocator::IsCluster(uint32_t cluster) const
    {
        return volume.geometry.IsCluster(cluster);
    }

    bool FatAllocator::Available(uint32_t cluster) const
    {
        return !reservations.Reserved(cluster) || (own != nullptr && own->Contains(cluster));
    }

    uint32_t FatAllocator::After(uint32_t cluster) const
    {
        return IsCluster(cluster + 1) ? cluster + 1 : 0;
    }
}
