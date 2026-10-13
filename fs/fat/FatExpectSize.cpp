#include "fs/fat/FatExpectSize.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatExpectSize::FatExpectSize(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, uint32_t size, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , file(file)
        , extents(extents)
        , size(size)
        , onDone(onDone)
        , search(volume.Cache(), volume.State(), volume.Reservations())
    {}

    FatExpectSize::~FatExpectSize() = default;

    void FatExpectSize::Start()
    {
        auto clusterBytes = volume.Geometry().sectorsPerCluster * volume.Cache().SectorSize();
        auto clusters = (uint64_t{ size } + clusterBytes - 1) / clusterBytes;
        file.reservation = FatReservation{};

        if (clusters <= file.clusters)
        {
            onDone(Result::success);
            return;
        }

        wanted = static_cast<uint32_t>(clusters - file.clusters);

        if (file.clusters == 0)
        {
            Search(std::nullopt);
            return;
        }

        volume.Locator().Locate(extents, file.clusters - 1, file.clusters - 1, [this](Result result, const FatExtent& run)
            {
                if (result != Result::success)
                    onDone(result);
                else
                    Search(run.ClusterAt(file.clusters - 1));
            });
    }

    void FatExpectSize::Search(std::optional<uint32_t> after)
    {
        search.Find(after, volume.Allocator().Cursor(), wanted, volume.State().allocationUnits, [this](Result result, FatReservation run)
            {
                if (result == Result::success)
                    file.reservation = run;

                onDone(result);
            });
    }
}
