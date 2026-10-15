#ifndef FS_NOR_NOR_BACKGROUND_HPP
#define FS_NOR_NOR_BACKGROUND_HPP

#include "fs/common/OperationQueue.hpp"
#include "services/fsm/StateMachineDefinition.hpp"
#include "services/fsm/TableStateMachine.hpp"
#include <array>
#include <cstdint>
#include <variant>

namespace fs
{
    class NorVolume;

    struct NorBackgroundStopped
    {
        static constexpr const char* name{ "Stopped" };
    };

    struct NorBackgroundWaiting
    {
        static constexpr const char* name{ "Waiting" };
    };

    struct NorBackgroundClaiming
    {
        static constexpr const char* name{ "Claiming" };
    };

    struct NorBackgroundWorking
    {
        static constexpr const char* name{ "Working" };
    };

    using NorBackgroundState = std::variant<NorBackgroundStopped, NorBackgroundWaiting, NorBackgroundClaiming, NorBackgroundWorking>;

    struct NorBackgroundStart
    {
        static constexpr const char* name{ "Start" };
    };

    struct NorBackgroundStop
    {
        static constexpr const char* name{ "Stop" };
    };

    struct NorBackgroundIdle
    {
        static constexpr const char* name{ "Idle" };
    };

    struct NorBackgroundGranted
    {
        static constexpr const char* name{ "Granted" };
    };

    struct NorBackgroundUnitDone
    {
        static constexpr const char* name{ "UnitDone" };
    };

    struct NorBackgroundUrge
    {
        static constexpr const char* name{ "Urge" };
    };

    using NorBackgroundEvent = std::variant<NorBackgroundStart, NorBackgroundStop, NorBackgroundIdle, NorBackgroundGranted, NorBackgroundUnitDone, NorBackgroundUrge>;

    class NorBackground
    {
    public:
        using Machine = services::TableStateMachine<NorBackgroundState, NorBackgroundEvent, NorBackground>;
        using Initial = NorBackgroundStopped;

        static constexpr std::array<Machine::Transition, 9> Rows();
        static constexpr Machine::Rules Rules();

        explicit NorBackground(NorVolume& volume);
        NorBackground(const NorBackground& other) = delete;
        NorBackground& operator=(const NorBackground& other) = delete;
        ~NorBackground();

        void Start();
        void Stop();
        void Idle();
        void Urge();
        void CallReleased();

    private:
        bool HasWork() const;
        bool CheckpointDue() const;
        bool FallbackDue() const;
        bool MoveDue() const;
        bool PoolHoldsAMove() const;
        bool ShouldStop() const;
        void Claim();
        void ClaimAhead();
        void Work();
        void WriteCheckpoint();
        void MoveBlock();
        void Release();

    private:
        NorVolume& volume;
        OperationQueue::BackgroundClaimer claimer;
        bool erasing{ false };
        bool urgent{ false };
        Machine::WithStorage<2> machine;
    };

    constexpr std::array<NorBackground::Machine::Transition, 9> NorBackground::Rows()
    {
        return {
            Machine::Row<NorBackgroundStopped, NorBackgroundStart, NorBackgroundWaiting>(),
            Machine::Row<NorBackgroundWaiting, NorBackgroundIdle, NorBackgroundClaiming>([](const NorBackground& background, const NorBackgroundWaiting&, const NorBackgroundIdle&)
                {
                    return background.HasWork();
                },
                [](NorBackground& background, NorBackgroundWaiting&, const NorBackgroundIdle&)
                {
                    background.Claim();
                    return NorBackgroundClaiming{};
                }),
            Machine::Row<NorBackgroundWaiting, NorBackgroundUrge, NorBackgroundClaiming>(nullptr, [](NorBackground& background, NorBackgroundWaiting&, const NorBackgroundUrge&)
                {
                    background.ClaimAhead();
                    return NorBackgroundClaiming{};
                }),
            Machine::Row<NorBackgroundWaiting, NorBackgroundStop, NorBackgroundStopped>(),
            Machine::Row<NorBackgroundClaiming, NorBackgroundStop, NorBackgroundStopped>(nullptr, [](NorBackground& background, NorBackgroundClaiming&, const NorBackgroundStop&)
                {
                    background.Release();
                    return NorBackgroundStopped{};
                }),
            Machine::Row<NorBackgroundClaiming, NorBackgroundGranted, NorBackgroundWaiting>([](const NorBackground& background, const NorBackgroundClaiming&, const NorBackgroundGranted&)
                {
                    return background.ShouldStop();
                },
                [](NorBackground& background, NorBackgroundClaiming&, const NorBackgroundGranted&)
                {
                    background.Release();
                    return NorBackgroundWaiting{};
                }),
            Machine::Row<NorBackgroundClaiming, NorBackgroundGranted, NorBackgroundWorking>(nullptr, [](NorBackground& background, NorBackgroundClaiming&, const NorBackgroundGranted&)
                {
                    background.Work();
                    return NorBackgroundWorking{};
                }),
            Machine::Row<NorBackgroundWorking, NorBackgroundUnitDone, NorBackgroundWaiting>([](const NorBackground& background, const NorBackgroundWorking&, const NorBackgroundUnitDone&)
                {
                    return background.ShouldStop();
                },
                [](NorBackground& background, NorBackgroundWorking&, const NorBackgroundUnitDone&)
                {
                    background.Release();
                    return NorBackgroundWaiting{};
                }),
            Machine::InternalRow<NorBackgroundWorking, NorBackgroundUnitDone>([](NorBackground& background, NorBackgroundWorking&, const NorBackgroundUnitDone&)
                {
                    background.Work();
                }),
        };
    }

    constexpr NorBackground::Machine::Rules NorBackground::Rules()
    {
        return Machine::Rules{}
            .Allow<NorBackgroundStopped, NorBackgroundWaiting>()
            .Allow<NorBackgroundWaiting, NorBackgroundClaiming, NorBackgroundStopped>()
            .Allow<NorBackgroundClaiming, NorBackgroundWaiting, NorBackgroundWorking, NorBackgroundStopped>()
            .Allow<NorBackgroundWorking, NorBackgroundWaiting>();
    }
}

#endif
