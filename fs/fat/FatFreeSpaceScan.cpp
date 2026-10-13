#include "fs/fat/FatFreeSpaceScan.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatFreeSpaceScan::FatFreeSpaceScan(SectorCache& cache, FatVolumeState& volume, infra::MemoryRange<FatReservation> runStorage)
        : cache(cache)
        , volume(volume)
        , runs(runStorage)
    {}

    FatFreeSpaceScan::~FatFreeSpaceScan() = default;

    void FatFreeSpaceScan::Restart()
    {
        sector = 0;
        cluster = 2;
        freeClusters = 0;
        finished = false;
        runs.Clear();
    }

    bool FatFreeSpaceScan::Done() const
    {
        return finished;
    }

    void FatFreeSpaceScan::Step(const infra::Function<void(Result result)>& onDone)
    {
        really_assert(!Done());

        this->onDone = onDone;
        cache.ReadOnce(volume.geometry.ActiveFatSector() + sector, [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatFreeSpaceScan::Allocated(uint32_t taken)
    {
        runs.Take(taken);

        if (volume.freeClusters.has_value())
            --*volume.freeClusters;
        else if (taken < cluster)
            --freeClusters;
    }

    void FatFreeSpaceScan::Freed(uint32_t released)
    {
        if (volume.freeClusters.has_value())
            ++*volume.freeClusters;
        else if (released < cluster)
            ++freeClusters;
    }

    const FatFreeRuns& FatFreeSpaceScan::Runs() const
    {
        return runs;
    }

    void FatFreeSpaceScan::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result == Result::success)
            Count(contents);

        onDone(result);
    }

    void FatFreeSpaceScan::Count(infra::ConstByteRange contents)
    {
        const auto& geometry = volume.geometry;
        auto end = geometry.clusterCount + 2;

        for (; cluster != end; ++cluster)
        {
            auto location = LocateEntry(geometry.type, cluster, cache.SectorSize());

            if (location.sector != sector)
                break;

            if (!location.straddles && ReadEntry(geometry.type, cluster, contents, infra::ConstByteRange()) == 0)
            {
                ++freeClusters;
                runs.Extend(cluster);
            }
            else
                runs.Close();
        }

        ++sector;

        if (cluster == end)
            Finish();
    }

    void FatFreeSpaceScan::Finish()
    {
        runs.Close();
        finished = true;

        if (!volume.freeClusters.has_value())
            volume.freeClusters = freeClusters;
    }
}
