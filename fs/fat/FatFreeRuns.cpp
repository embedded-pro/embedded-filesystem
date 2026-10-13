#include "fs/fat/FatFreeRuns.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        uint32_t EndOf(const FatReservation& run)
        {
            return run.first + run.count;
        }

        uint32_t UnitAtOrAfter(const FatAllocationUnits& units, uint32_t cluster)
        {
            return units.IsBoundary(cluster) ? cluster : units.NextBoundaryAfter(cluster);
        }

        bool Unreserved(uint32_t first, uint32_t count, const FatReservations& reservations)
        {
            for (auto cluster = first; cluster != first + count; ++cluster)
                if (reservations.Reserved(cluster))
                    return false;

            return true;
        }

        std::optional<uint32_t> FirstUnit(const FatReservation& run, uint32_t from, const FatAllocationUnits& units, const FatReservations& reservations)
        {
            for (auto boundary = UnitAtOrAfter(units, std::max(run.first, from)); boundary + units.clustersPerUnit <= EndOf(run); boundary += units.clustersPerUnit)
                if (Unreserved(boundary, units.clustersPerUnit, reservations))
                    return boundary;

            return std::nullopt;
        }

        void KeepEarliest(std::optional<uint32_t>& earliest, std::optional<uint32_t> candidate)
        {
            if (candidate.has_value() && (!earliest.has_value() || *candidate < *earliest))
                earliest = candidate;
        }

        void KeepIfLonger(infra::MemoryRange<FatReservation> storage, const FatReservation& run)
        {
            auto shortest = std::ranges::min_element(storage, {}, &FatReservation::count);

            if (shortest != storage.end() && shortest->count < run.count)
                *shortest = run;
        }
    }

    FatFreeRuns::FatFreeRuns(infra::MemoryRange<FatReservation> storage)
        : storage(storage)
    {}

    FatFreeRuns::~FatFreeRuns() = default;

    void FatFreeRuns::Clear()
    {
        std::ranges::fill(storage, FatReservation{});
        extending = FatReservation{};
    }

    void FatFreeRuns::Extend(uint32_t cluster)
    {
        if (extending.count != 0 && EndOf(extending) == cluster)
            ++extending.count;
        else
        {
            Close();
            extending = FatReservation{ cluster, 1 };
        }
    }

    void FatFreeRuns::Close()
    {
        KeepIfLonger(storage, extending);
        extending = FatReservation{};
    }

    void FatFreeRuns::Take(uint32_t cluster)
    {
        if (extending.Contains(cluster))
        {
            KeepIfLonger(storage, FatReservation{ extending.first, cluster - extending.first });
            extending = FatReservation{ cluster + 1, EndOf(extending) - cluster - 1 };
            return;
        }

        auto run = std::ranges::find_if(storage, [cluster](const FatReservation& candidate)
            {
                return candidate.Contains(cluster);
            });

        if (run == storage.end())
            return;

        FatReservation after{ cluster + 1, EndOf(*run) - cluster - 1 };
        run->count = cluster - run->first;
        KeepIfLonger(storage, after);
    }

    std::optional<uint32_t> FatFreeRuns::WholeUnit(const FatAllocationUnits& units, uint32_t from, const FatReservations& reservations) const
    {
        std::optional<uint32_t> atOrAfter;
        std::optional<uint32_t> lowest;

        auto consider = [&atOrAfter, &lowest, &units, &reservations, from](const FatReservation& run)
        {
            KeepEarliest(atOrAfter, FirstUnit(run, from, units, reservations));
            KeepEarliest(lowest, FirstUnit(run, run.first, units, reservations));
        };

        for (const auto& run : storage)
            consider(run);

        consider(extending);

        return atOrAfter.has_value() ? atOrAfter : lowest;
    }

}
