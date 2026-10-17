#ifndef FS_NOR_NOR_FOREGROUND_LEVELING_HPP
#define FS_NOR_NOR_FOREGROUND_LEVELING_HPP

#include "fs/common/OperationQueue.hpp"
#include "fs/nor/NorBackgroundWork.hpp"
#include "services/fsm/StateMachineDefinition.hpp"
#include "services/fsm/TableStateMachine.hpp"
#include <array>
#include <variant>

namespace fs
{
    class NorVolume;

    struct NorLevelingStopped
    {
        static constexpr const char* name{ "Stopped" };
    };

    struct NorLevelingWaiting
    {
        static constexpr const char* name{ "Waiting" };
    };

    struct NorLevelingClaiming
    {
        static constexpr const char* name{ "Claiming" };
    };

    struct NorLevelingMoving
    {
        static constexpr const char* name{ "Moving" };
    };

    using NorLevelingState = std::variant<NorLevelingStopped, NorLevelingWaiting, NorLevelingClaiming, NorLevelingMoving>;

    struct NorLevelingStart
    {
        static constexpr const char* name{ "Start" };
    };

    struct NorLevelingStop
    {
        static constexpr const char* name{ "Stop" };
    };

    struct NorLevelingDue
    {
        static constexpr const char* name{ "Due" };
    };

    struct NorLevelingGranted
    {
        static constexpr const char* name{ "Granted" };
    };

    struct NorLevelingMoved
    {
        static constexpr const char* name{ "Moved" };
    };

    using NorLevelingEvent = std::variant<NorLevelingStart, NorLevelingStop, NorLevelingDue, NorLevelingGranted, NorLevelingMoved>;

    class NorForegroundLeveling
        : public NorBackgroundWork
    {
    public:
        using Machine = services::TableStateMachine<NorLevelingState, NorLevelingEvent, NorForegroundLeveling>;
        using Initial = NorLevelingStopped;

        static constexpr std::array<Machine::Transition, 6> Rows();
        static constexpr Machine::Rules Rules();

        explicit NorForegroundLeveling(NorVolume& volume);
        NorForegroundLeveling(const NorForegroundLeveling& other) = delete;
        NorForegroundLeveling& operator=(const NorForegroundLeveling& other) = delete;
        ~NorForegroundLeveling();

        void Start() override;
        void Stop() override;
        void Urge() override;
        void ForegroundClaiming() override;
        void ForegroundReleased() override;
        bool Scheduled() const override;

    private:
        bool MoveDue() const;
        void ClaimAhead();
        void Move();
        void Release();

    private:
        NorVolume& volume;
        OperationQueue::BackgroundClaimer claimer;
        bool armed{ false };
        Machine::WithStorage<2> machine;
    };

    constexpr std::array<NorForegroundLeveling::Machine::Transition, 6> NorForegroundLeveling::Rows()
    {
        return {
            Machine::Row<NorLevelingStopped, NorLevelingStart, NorLevelingWaiting>(),
            Machine::Row<NorLevelingWaiting, NorLevelingDue, NorLevelingClaiming>(nullptr, [](NorForegroundLeveling& leveling, NorLevelingWaiting&, const NorLevelingDue&)
                {
                    leveling.ClaimAhead();
                    return NorLevelingClaiming{};
                }),
            Machine::Row<NorLevelingWaiting, NorLevelingStop, NorLevelingStopped>(),
            Machine::Row<NorLevelingClaiming, NorLevelingStop, NorLevelingStopped>(nullptr, [](NorForegroundLeveling& leveling, NorLevelingClaiming&, const NorLevelingStop&)
                {
                    leveling.Release();
                    return NorLevelingStopped{};
                }),
            Machine::Row<NorLevelingClaiming, NorLevelingGranted, NorLevelingMoving>(nullptr, [](NorForegroundLeveling& leveling, NorLevelingClaiming&, const NorLevelingGranted&)
                {
                    leveling.Move();
                    return NorLevelingMoving{};
                }),
            Machine::Row<NorLevelingMoving, NorLevelingMoved, NorLevelingWaiting>(nullptr, [](NorForegroundLeveling& leveling, NorLevelingMoving&, const NorLevelingMoved&)
                {
                    leveling.Release();
                    return NorLevelingWaiting{};
                }),
        };
    }

    constexpr NorForegroundLeveling::Machine::Rules NorForegroundLeveling::Rules()
    {
        return Machine::Rules{}
            .Allow<NorLevelingStopped, NorLevelingWaiting>()
            .Allow<NorLevelingWaiting, NorLevelingClaiming, NorLevelingStopped>()
            .Allow<NorLevelingClaiming, NorLevelingMoving, NorLevelingStopped>()
            .Allow<NorLevelingMoving, NorLevelingWaiting>();
    }
}

#endif
