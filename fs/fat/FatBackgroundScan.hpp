#ifndef FS_FAT_FAT_BACKGROUND_SCAN_HPP
#define FS_FAT_FAT_BACKGROUND_SCAN_HPP

#include "fs/common/OperationQueue.hpp"
#include "fs/fat/FatFreeSpaceScan.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include "services/fsm/StateMachineDefinition.hpp"
#include "services/fsm/TableStateMachine.hpp"
#include <array>
#include <variant>

namespace fs
{
    struct FatScanStopped
    {
        static constexpr const char* name{ "Stopped" };
    };

    struct FatScanUnknown
    {
        static constexpr const char* name{ "Unknown" };
    };

    struct FatScanClaiming
    {
        static constexpr const char* name{ "Claiming" };
    };

    struct FatScanScanning
    {
        static constexpr const char* name{ "Scanning" };
    };

    struct FatScanCurrent
    {
        static constexpr const char* name{ "Current" };
    };

    using FatScanState = std::variant<FatScanStopped, FatScanUnknown, FatScanClaiming, FatScanScanning, FatScanCurrent>;

    struct FatScanStart
    {
        static constexpr const char* name{ "Start" };
    };

    struct FatScanStop
    {
        static constexpr const char* name{ "Stop" };
    };

    struct FatScanIdle
    {
        static constexpr const char* name{ "Idle" };
    };

    struct FatScanGranted
    {
        static constexpr const char* name{ "Granted" };
    };

    struct FatScanStepped
    {
        static constexpr const char* name{ "Stepped" };
        Result result{ Result::success };
    };

    using FatScanEvent = std::variant<FatScanStart, FatScanStop, FatScanIdle, FatScanGranted, FatScanStepped>;

    class FatBackgroundScan
    {
    public:
        using Machine = services::TableStateMachine<FatScanState, FatScanEvent, FatBackgroundScan>;
        using Initial = FatScanStopped;

        static constexpr std::array<Machine::Transition, 14> Rows();
        static constexpr Machine::Rules Rules();

        FatBackgroundScan(OperationQueue& queue, FatFreeSpaceScan& scan, const infra::Function<void(Result result)>& onFailed);
        FatBackgroundScan(const FatBackgroundScan& other) = delete;
        FatBackgroundScan& operator=(const FatBackgroundScan& other) = delete;
        ~FatBackgroundScan();

        void Start();
        void Stop();
        void Idle();

    private:
        void Claim();
        void Step();
        void Release();
        void Failed(Result result);
        bool ShouldYield() const;
        bool Done() const;

    private:
        OperationQueue::BackgroundClaimer claimer;
        FatFreeSpaceScan& scan;
        infra::Function<void(Result result)> onFailed;
        Machine::WithStorage<2> machine;
    };

    constexpr std::array<FatBackgroundScan::Machine::Transition, 14> FatBackgroundScan::Rows()
    {
        return {
            Machine::Row<FatScanStopped, FatScanStart, FatScanUnknown>(),
            Machine::InternalRow<FatScanStopped, FatScanStop>(),
            Machine::InternalRow<FatScanStopped, FatScanIdle>(),
            Machine::Row<FatScanUnknown, FatScanIdle, FatScanCurrent>([](FatBackgroundScan& background, const FatScanUnknown&, const FatScanIdle&)
                {
                    return background.Done();
                }),
            Machine::Row<FatScanUnknown, FatScanIdle, FatScanClaiming>(nullptr, [](FatBackgroundScan& background, FatScanUnknown&, const FatScanIdle&)
                {
                    background.Claim();
                    return FatScanClaiming{};
                }),
            Machine::Row<FatScanUnknown, FatScanStop, FatScanStopped>(),
            Machine::Row<FatScanClaiming, FatScanGranted, FatScanUnknown>([](FatBackgroundScan& background, const FatScanClaiming&, const FatScanGranted&)
                {
                    return background.ShouldYield();
                },
                [](FatBackgroundScan& background, FatScanClaiming&, const FatScanGranted&)
                {
                    background.Release();
                    return FatScanUnknown{};
                }),
            Machine::Row<FatScanClaiming, FatScanGranted, FatScanScanning>(nullptr, [](FatBackgroundScan& background, FatScanClaiming&, const FatScanGranted&)
                {
                    background.Step();
                    return FatScanScanning{};
                }),
            Machine::Row<FatScanScanning, FatScanStepped, FatScanUnknown>([](FatBackgroundScan&, const FatScanScanning&, const FatScanStepped& stepped)
                {
                    return stepped.result != Result::success;
                },
                [](FatBackgroundScan& background, FatScanScanning&, const FatScanStepped& stepped)
                {
                    background.Release();
                    background.Failed(stepped.result);
                    return FatScanUnknown{};
                }),
            Machine::Row<FatScanScanning, FatScanStepped, FatScanCurrent>([](FatBackgroundScan& background, const FatScanScanning&, const FatScanStepped&)
                {
                    return background.Done();
                },
                [](FatBackgroundScan& background, FatScanScanning&, const FatScanStepped&)
                {
                    background.Release();
                    return FatScanCurrent{};
                }),
            Machine::Row<FatScanScanning, FatScanStepped, FatScanUnknown>([](FatBackgroundScan& background, const FatScanScanning&, const FatScanStepped&)
                {
                    return background.ShouldYield();
                },
                [](FatBackgroundScan& background, FatScanScanning&, const FatScanStepped&)
                {
                    background.Release();
                    return FatScanUnknown{};
                }),
            Machine::InternalRow<FatScanScanning, FatScanStepped>([](FatBackgroundScan& background, FatScanScanning&, const FatScanStepped&)
                {
                    background.Step();
                }),
            Machine::InternalRow<FatScanCurrent, FatScanIdle>(),
            Machine::Row<FatScanCurrent, FatScanStop, FatScanStopped>(),
        };
    }

    constexpr FatBackgroundScan::Machine::Rules FatBackgroundScan::Rules()
    {
        return Machine::Rules{}
            .Allow<FatScanStopped, FatScanUnknown>()
            .Allow<FatScanUnknown, FatScanCurrent, FatScanClaiming, FatScanStopped>()
            .Allow<FatScanClaiming, FatScanUnknown, FatScanScanning>()
            .Allow<FatScanScanning, FatScanUnknown, FatScanCurrent>()
            .Allow<FatScanCurrent, FatScanStopped>();
    }
}

#endif
