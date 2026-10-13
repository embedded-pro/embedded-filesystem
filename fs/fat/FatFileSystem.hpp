#ifndef FS_FAT_FAT_FILE_SYSTEM_HPP
#define FS_FAT_FAT_FILE_SYSTEM_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/FlushBarrier.hpp"
#include "fs/common/HandlePool.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/common/Path.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatClosedFiles.hpp"
#include "fs/fat/FatConfig.hpp"
#include "fs/fat/FatDirectory.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatLifecycle.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/fat/FatVolume.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "hal/interfaces/BlockDevice.hpp"
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

        static constexpr std::array<Machine::Transition, 7> Rows();
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
        FatFileSystem(const FatVolumeParts& parts, const FatConfig& config);
        FatFileSystem(const FatFileSystem& other) = delete;
        FatFileSystem& operator=(const FatFileSystem& other) = delete;
        ~FatFileSystem();

        FatVolume& Volume();
        void AttachPathSlots(infra::MemoryRange<FatPathSlot> slots);

    private:
        virtual infra::SharedPtr<FatDirectory> AllocateDirectory() = 0;
        virtual infra::SharedPtr<FatFile> AllocateFile() = 0;

        Result Unavailable() const;
        bool IsMounted() const;

        void BeginMount();
        void StartMount();
        void BeginUnmount();
        void StartUnmount();
        void LifecycleOperationDone(Result result);
        void Entered(Result result);
        void DeferCompletion(infra::Function<void(Result result)>& onDone, Result result);

        template<class Call>
        void StartPathCall(infra::BoundedConstString path, Call&& call);
        FatPathSlot* FreePathSlot() const;
        void RunPathCall(FatPathSlot& slot);
        void RunOperation(FatPathSlot& slot, FatPathCall& call);

    private:
        FatVolume volume;
        OperationQueue::Claimer lifecycleClaimer;
        infra::MemoryRange<FatPathSlot> pathSlots;
        Machine::WithStorage<2> lifecycle;
        infra::Function<void(Result result)> onMountDone;
        infra::Function<void(Result result)> onUnmountDone;
        infra::Function<void(Result result)> lifecycleDone;
        Result lifecycleResult{ Result::success };
    };

    namespace detail
    {
        template<std::size_t CacheSectors, std::size_t Completions, std::size_t NameBytes, std::size_t ClosedFiles>
        struct FatVolumeStorage
        {
            explicit FatVolumeStorage(hal::BlockDevice& device)
                : barrierStorage(device)
                , cacheStorage(barrierStorage)
            {}

            FlushBarrier barrierStorage;
            SectorCache::WithStorage<CacheSectors> cacheStorage;
            DeferredCompletions::WithMaxSize<Completions> deferredStorage;
            std::array<uint8_t, NameBytes> nameStorage{};
            std::array<FatClosedFile, ClosedFiles> closedStorage{};
        };
    }

    template<std::size_t Files, std::size_t CacheSectors, FatLimits Limits>
    class FatFileSystem::WithStorage
        : private detail::FatVolumeStorage<CacheSectors, Files + Limits.directories + Limits.pathOperations + 2, Limits.maxNameBytes, Limits.closedFiles>
        , public FatFileSystem
    {
        static_assert(Files > 0 && CacheSectors > 2 && Limits.directories > 0 && Limits.pathOperations > 0 && Limits.extentsPerFile > 0);

        using Storage = detail::FatVolumeStorage<CacheSectors, Files + Limits.directories + Limits.pathOperations + 2, Limits.maxNameBytes, Limits.closedFiles>;

    public:
        explicit WithStorage(hal::BlockDevice& device, const FatConfig& config = {})
            : Storage(device)
            , FatFileSystem(FatVolumeParts{ device, this->barrierStorage, this->cacheStorage, this->deferredStorage, infra::MakeRange(this->nameStorage), infra::MakeRange(this->closedStorage) }, config)
        {
            for (std::size_t index = 0; index != Limits.pathOperations; ++index)
                pathSlots.emplace_back(Volume().Queue());

            AttachPathSlots(infra::MakeRange(pathSlots));
        }

    private:
        infra::SharedPtr<FatDirectory> AllocateDirectory() override
        {
            return directories.Allocate();
        }

        infra::SharedPtr<FatFile> AllocateFile() override
        {
            return files.Allocate();
        }

    private:
        typename infra::BoundedVector<FatPathSlot>::template WithMaxSize<Limits.pathOperations> pathSlots;
        HandlePool<FatDirectory, Limits.directories> directories{ Volume() };
        HandlePool<FatFile::WithExtents<Limits.extentsPerFile>, Files> files{ Volume() };
    };

    constexpr std::array<FatFileSystem::Machine::Transition, 7> FatFileSystem::Rows()
    {
        return {
            Machine::Row<FatUnmounted, FatMountRequested, FatMounting>(nullptr, [](FatFileSystem& volume, FatUnmounted&, const FatMountRequested&)
                {
                    volume.BeginMount();
                    return FatMounting{};
                }),
            Machine::Row<FatFault, FatMountRequested, FatMounting>(nullptr, [](FatFileSystem& volume, FatFault&, const FatMountRequested&)
                {
                    volume.BeginMount();
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
            Machine::Row<FatMounted, FatUnmountRequested, FatUnmounting>(nullptr, [](FatFileSystem& volume, FatMounted&, const FatUnmountRequested&)
                {
                    volume.BeginUnmount();
                    return FatUnmounting{};
                }),
            Machine::Row<FatMounted, FatMediaRemoved, FatFault>(nullptr, [](FatFileSystem&, FatMounted&, const FatMediaRemoved&)
                {
                    return FatFault{ Result::mediaRemoved };
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
            .Allow<FatMounted, FatUnmounting, FatFault>()
            .Allow<FatUnmounting, FatUnmounted>()
            .Allow<FatFault, FatMounting>();
    }
}

#endif
