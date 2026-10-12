#ifndef FS_FAT_FAT_FILE_SYSTEM_HPP
#define FS_FAT_FAT_FILE_SYSTEM_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/FlushBarrier.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/common/Path.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatConfig.hpp"
#include "fs/fat/FatLifecycle.hpp"
#include "fs/fat/FatMount.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/fat/FatVolumeState.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/fsm/StateMachineDefinition.hpp"
#include "services/fsm/TableStateMachine.hpp"
#include <array>
#include <cstddef>
#include <variant>

namespace fs
{
    class FatFileSystem
        : public FileSystem
    {
    public:
        template<std::size_t Files, std::size_t CacheSectors, FatLimits Limits = FatLimits{}>
        class WithStorage;

        using Machine = services::TableStateMachine<FatLifecycleState, FatLifecycleEvent, FatFileSystem>;
        using Initial = FatUnmounted;

        static constexpr std::array<Machine::Transition, 6> Rows();
        static constexpr Machine::Rules Rules();

        void Mount(const infra::Function<void(Result result)>& onDone) override;
        void Unmount(const infra::Function<void(Result result)>& onDone) override;
        void Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone) override;
        void OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone) override;
        void Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone) override;
        void Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone) override;
        void MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone) override;

    protected:
        FatFileSystem(hal::BlockDevice& device, SectorCache& cache, DeferredCompletions& deferred, const FatConfig& config, std::size_t maxNameBytes);
        FatFileSystem(const FatFileSystem& other) = delete;
        FatFileSystem& operator=(const FatFileSystem& other) = delete;
        ~FatFileSystem();

        OperationQueue& Queue();
        void AttachPathSlots(infra::MemoryRange<FatPathSlot> slots);

    private:
        template<class Callback>
        void Reject(const Callback& onDone, Result result);
        Result Unavailable() const;
        bool IsMounted() const;

        void BeginMount(const FatMountRequested& request);
        void StartMount();
        void BeginUnmount(const FatUnmountRequested& request);
        void StartUnmount();
        void LifecycleOperationDone(Result result);
        void Entered(Result result);

        FatPathSlot* FreePathSlot();
        void StartPathCall(FatPathSlot& slot, FatPathCall&& call);
        void RunPathCall(FatPathSlot& slot);
        void RunStat(const FatStatCall& call);

    private:
        hal::BlockDevice& device;
        SectorCache& cache;
        DeferredCompletions& deferred;
        FatConfig config;
        PathLimits pathLimits;
        FatVolumeState volume;
        OperationQueue queue;
        OperationQueue::Claimer lifecycleClaimer;
        infra::MemoryRange<FatPathSlot> pathSlots;
        Machine::WithStorage<2> lifecycle;
        infra::AutoResetFunction<void(Result result)> onMountDone;
        infra::AutoResetFunction<void(Result result)> onUnmountDone;
        infra::Function<void(Result result)> lifecycleDone;
        Result lifecycleResult{ Result::success };
        std::variant<std::monostate, FatMount> operation;
    };

    namespace detail
    {
        template<std::size_t CacheSectors, std::size_t Completions>
        struct FatVolumeStorage
        {
            explicit FatVolumeStorage(hal::BlockDevice& device)
                : barrierStorage(device)
                , cacheStorage(barrierStorage)
            {}

            FlushBarrier barrierStorage;
            SectorCache::WithStorage<CacheSectors> cacheStorage;
            DeferredCompletions::WithMaxSize<Completions> deferredStorage;
        };
    }

    template<std::size_t Files, std::size_t CacheSectors, FatLimits Limits>
    class FatFileSystem::WithStorage
        : private detail::FatVolumeStorage<CacheSectors, Files + Limits.directories + Limits.pathOperations + 2>
        , public FatFileSystem
    {
        static_assert(Files > 0 && CacheSectors > 2 && Limits.pathOperations > 0);

    public:
        explicit WithStorage(hal::BlockDevice& device, const FatConfig& config = {})
            : detail::FatVolumeStorage<CacheSectors, Files + Limits.directories + Limits.pathOperations + 2>(device)
            , FatFileSystem(device, this->cacheStorage, this->deferredStorage, config, Limits.maxNameBytes)
        {
            for (std::size_t index = 0; index != Limits.pathOperations; ++index)
                pathSlots.emplace_back(Queue());

            AttachPathSlots(infra::MakeRange(pathSlots));
        }

    private:
        typename infra::BoundedVector<FatPathSlot>::template WithMaxSize<Limits.pathOperations> pathSlots;
    };

    constexpr std::array<FatFileSystem::Machine::Transition, 6> FatFileSystem::Rows()
    {
        return {
            Machine::Row<FatUnmounted, FatMountRequested, FatMounting>(nullptr, [](FatFileSystem& volume, FatUnmounted&, const FatMountRequested& request)
                {
                    volume.BeginMount(request);
                    return FatMounting{};
                }),
            Machine::Row<FatFault, FatMountRequested, FatMounting>(nullptr, [](FatFileSystem& volume, FatFault&, const FatMountRequested& request)
                {
                    volume.BeginMount(request);
                    return FatMounting{};
                }),
            Machine::Row<FatMounting, FatMountDone, FatMounted>([](FatFileSystem&, const FatMounting&, const FatMountDone& done)
                {
                    return done.result == Result::success;
                }),
            Machine::Row<FatMounting, FatMountDone, FatFault>(nullptr, [](FatFileSystem&, FatMounting&, const FatMountDone& done)
                {
                    return FatFault{ done.result };
                }),
            Machine::Row<FatMounted, FatUnmountRequested, FatUnmounting>(nullptr, [](FatFileSystem& volume, FatMounted&, const FatUnmountRequested& request)
                {
                    volume.BeginUnmount(request);
                    return FatUnmounting{};
                }),
            Machine::Row<FatUnmounting, FatUnmountDone, FatUnmounted>(nullptr, [](FatFileSystem& volume, FatUnmounting&, const FatUnmountDone& done)
                {
                    volume.lifecycleResult = done.result;
                    return FatUnmounted{};
                }),
        };
    }

    constexpr FatFileSystem::Machine::Rules FatFileSystem::Rules()
    {
        return Machine::Rules{}
            .Allow<FatUnmounted, FatMounting>()
            .Allow<FatMounting, FatMounted, FatFault>()
            .Allow<FatMounted, FatUnmounting>()
            .Allow<FatUnmounting, FatUnmounted>()
            .Allow<FatFault, FatMounting>();
    }
}

#endif
