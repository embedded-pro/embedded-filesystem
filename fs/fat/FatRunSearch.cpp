#include "fs/fat/FatRunSearch.hpp"
#include "fs/fat/FatTable.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t firstCluster = 2;
    }

    FatRunSearch::FatRunSearch(SectorCache& cache, const FatVolumeState& volume, const FatReservations& reservations)
        : cache(cache)
        , volume(volume)
        , reservations(reservations)
    {}

    FatRunSearch::~FatRunSearch() = default;

    void FatRunSearch::Find(std::optional<uint32_t> after, uint32_t cursor, uint32_t wanted, const std::optional<FatAllocationUnits>& units, const infra::Function<void(Result result, FatReservation run)>& onDone)
    {
        this->cursor = cursor;
        this->wanted = wanted;
        this->units = units;
        this->onDone = onDone;
        target = wanted;
        examined = 0;
        run = FatReservation{};
        best = FatReservation{};
        adjacent = after && After(*after) != 0;
        candidate = adjacent ? After(*after) : cursor;
        run.first = candidate;
        ReadSector();
    }

    void FatRunSearch::ReadSector()
    {
        cache.Read(SectorOf(candidate), [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatRunSearch::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            onDone(result, FatReservation{});
            return;
        }

        auto sector = SectorOf(candidate);

        while (SectorOf(candidate) == sector)
        {
            auto cluster = candidate;
            auto free = IsFree(cluster, contents);

            if (adjacent ? Adjacent(cluster, free) : Search(cluster, free))
                return;
        }

        ReadSector();
    }

    bool FatRunSearch::Adjacent(uint32_t cluster, bool free)
    {
        if (free && ++run.count == wanted)
            return Finish(run);

        if (free && After(cluster) != 0)
        {
            candidate = After(cluster);
            return false;
        }

        adjacent = false;
        run = FatReservation{};
        candidate = cursor;
        ReadSector();
        return true;
    }

    bool FatRunSearch::Search(uint32_t cluster, bool free)
    {
        ++examined;

        if (free)
        {
            if (run.count++ == 0)
                run.first = cluster;

            if (run.count > best.count)
                best = run;

            if (run.count == target && Reached())
                return true;
        }
        else if (target != wanted)
            return Finish(FatReservation{ run.first, wanted });
        else
            run = FatReservation{};

        candidate = After(cluster);

        if (candidate == 0)
        {
            if (target != wanted)
                return Finish(FatReservation{ run.first, wanted });

            run = FatReservation{};
            candidate = firstCluster;
        }

        if (run.count == 0 && examined >= volume.geometry.clusterCount)
            return Finish(best);

        return false;
    }

    bool FatRunSearch::Reached()
    {
        if (target != wanted)
            return Finish(FatReservation{ run.first + target - wanted, wanted });

        if (units && !units->IsBoundary(run.first) && units->NextBoundaryAfter(run.first) < run.first + wanted)
        {
            target = units->NextBoundaryAfter(run.first) - run.first + wanted;
            return false;
        }

        return Finish(run);
    }

    bool FatRunSearch::Finish(FatReservation run)
    {
        onDone(Result::success, run);
        return true;
    }

    bool FatRunSearch::IsFree(uint32_t cluster, infra::ConstByteRange contents) const
    {
        const auto& geometry = volume.geometry;

        return !LocateEntry(geometry.type, cluster, cache.SectorSize()).straddles && ReadEntry(geometry.type, cluster, contents, infra::ConstByteRange()) == 0 && !reservations.Reserved(cluster);
    }

    uint32_t FatRunSearch::SectorOf(uint32_t cluster) const
    {
        return volume.geometry.ActiveFatSector() + LocateEntry(volume.geometry.type, cluster, cache.SectorSize()).sector;
    }

    uint32_t FatRunSearch::After(uint32_t cluster) const
    {
        return volume.geometry.IsCluster(cluster + 1) ? cluster + 1 : 0;
    }
}
