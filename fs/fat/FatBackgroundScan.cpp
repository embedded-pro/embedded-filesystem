#include "fs/fat/FatBackgroundScan.hpp"

namespace fs
{
    FatBackgroundScan::FatBackgroundScan(OperationQueue& queue, FatFreeSpaceScan& scan, const infra::Function<void(Result result)>& onFailed)
        : claimer(queue)
        , scan(scan)
        , onFailed(onFailed)
        , machine(*this, services::Validated<FatBackgroundScan>())
    {
        machine.Start<FatScanStopped>();
    }

    FatBackgroundScan::~FatBackgroundScan() = default;

    void FatBackgroundScan::Start()
    {
        machine.Dispatch(FatScanStart{});
    }

    void FatBackgroundScan::Stop()
    {
        machine.Dispatch(FatScanStop{});
    }

    void FatBackgroundScan::Idle()
    {
        machine.Dispatch(FatScanIdle{});
    }

    void FatBackgroundScan::Claim()
    {
        claimer.Claim(machine.CompletionWith<void()>([]
            {
                return FatScanGranted{};
            }));
    }

    void FatBackgroundScan::Step()
    {
        scan.Step(machine.CompletionWith<void(Result result)>([](Result result)
            {
                return FatScanStepped{ result };
            }));
    }

    void FatBackgroundScan::Release()
    {
        claimer.Release();
    }

    void FatBackgroundScan::Failed(Result result)
    {
        onFailed(result);
    }

    bool FatBackgroundScan::ShouldYield() const
    {
        return claimer.ShouldYield();
    }

    bool FatBackgroundScan::Done() const
    {
        return scan.Done();
    }
}
