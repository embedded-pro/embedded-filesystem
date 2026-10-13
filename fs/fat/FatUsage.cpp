#include "fs/fat/FatUsage.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatUsage::FatUsage(FatVolume& volume, FatUsageCall&& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , onFinished(onFinished)
    {}

    FatUsage::~FatUsage() = default;

    void FatUsage::Start()
    {
        if (volume.State().freeClusters.has_value())
            Complete(Result::success);
        else
            volume.FreeSpace().Step([this](Result result)
                {
                    OnStepped(result);
                });
    }

    void FatUsage::OnStepped(Result result)
    {
        if (result != Result::success)
            Complete(result);
        else
            Start();
    }

    void FatUsage::Complete(Result result)
    {
        onFinished(result);

        if (result != Result::success)
        {
            call.onDone(result, VolumeUsage{});
            return;
        }

        const auto& geometry = volume.Geometry();
        uint64_t clusterBytes = uint64_t{ geometry.sectorsPerCluster } * volume.Cache().SectorSize();
        auto freeClusters = *volume.State().freeClusters;

        call.onDone(result, VolumeUsage{ (geometry.clusterCount - freeClusters) * clusterBytes, geometry.clusterCount * clusterBytes });
    }
}
