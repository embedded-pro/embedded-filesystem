#include "fs/fat/FatFreeSpaceScan.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatFreeSpaceScan::FatFreeSpaceScan(SectorCache& cache, FatVolumeState& volume)
        : cache(cache)
        , volume(volume)
    {}

    FatFreeSpaceScan::~FatFreeSpaceScan() = default;

    void FatFreeSpaceScan::Restart()
    {
        sector = 0;
        cluster = 2;
        freeClusters = 0;
    }

    bool FatFreeSpaceScan::Done() const
    {
        return volume.freeClusters.has_value();
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
                ++freeClusters;
        }

        ++sector;

        if (cluster == end)
            volume.freeClusters = freeClusters;
    }
}
