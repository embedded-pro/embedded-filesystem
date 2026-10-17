#ifndef FS_NOR_NOR_FILE_SYSTEM_HPP
#define FS_NOR_NOR_FILE_SYSTEM_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/HandlePool.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "fs/nor/NorBackground.hpp"
#include "fs/nor/NorBlockStates.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorDirectory.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorForegroundLeveling.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorLifecycle.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/NorVolume.hpp"
#include "fs/nor/NorVolumeMount.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/fsm/StateMachineDefinition.hpp"
#include "services/fsm/TableStateMachine.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fs
{
    class NorFileSystem
        : public FileSystem
        , private NorVolumeMount<NorFileSystem>
    {
    public:
        template<NorConfig Config, std::size_t Files, NorFeatures Features = NorFeatures{}>
        class WithStorage;

        using Machine = services::TableStateMachine<NorLifecycleState, NorLifecycleEvent, NorFileSystem>;
        using Initial = NorUnmounted;

        static constexpr infra::Duration defaultIdleDelay{ std::chrono::milliseconds(10) };

        static constexpr std::array<Machine::Transition, 9> Rows();
        static constexpr Machine::Rules Rules();

        void Format(const infra::Function<void(Result result)>& onDone);
        NorCounters Counters() const;

        void Mount(const infra::Function<void(Result result)>& onDone) override;
        void Unmount(const infra::Function<void(Result result)>& onDone) override;
        void Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone) override;
        void OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone) override;
        void Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone) override;
        void Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone) override;
        void MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone) override;
        bool Idle() const override;

    protected:
        NorFileSystem(const NorVolumeParts& parts, const NorConfig& config);
        NorFileSystem(const NorFileSystem& other) = delete;
        NorFileSystem& operator=(const NorFileSystem& other) = delete;
        ~NorFileSystem();

        NorVolume& Volume();
        void AttachPathSlots(infra::MemoryRange<NorPathSlot> slots);

    private:
        friend class NorVolumeMount<NorFileSystem>;

        virtual infra::SharedPtr<NorDirectory> AllocateDirectory() = 0;
        virtual infra::SharedPtr<NorFile> AllocateFile() = 0;

        bool IsMounted() const;
        Result Refusal(infra::BoundedConstString path, bool supported = true) const;
        Result Refusal(Result validation, bool supported = true) const;
        void Start(NorPathCall&& call);
        NorPathSlot* FreePathSlot() const;
        void RunPathCall(NorPathSlot& slot);
        void RunOperation(NorPathSlot& slot, NorPathCall& call);
        void ReportUsage(NorPathSlot& slot, NorUsageCall& call);

        void BeginFormat();
        void StartFormat();
        void BeginMount();
        void BeginUnmount();
        void Mounted(Result result);
        void LifecycleOperationDone(Result result);
        void Entered(Result result);
        void DeferCompletion(infra::Function<void(Result result)>& onDone, Result result);

    private:
        NorVolume volume;
        OperationQueue::Claimer lifecycleClaimer;
        infra::MemoryRange<NorPathSlot> pathSlots;
        Machine::WithStorage<2> lifecycle;
        infra::Function<void(Result result)> onFormatDone;
        infra::Function<void(Result result)> onMountDone;
        infra::Function<void(Result result)> onUnmountDone;
        infra::Function<void(Result result)> lifecycleDone;
        Result lifecycleResult{ Result::success };
    };

    namespace detail
    {
        template<class Operation, class Call>
        void StartNorOperation(NorVolume& volume, Call&& call, const infra::Function<void(Result result)>& onFinished);

        void StartNorTruncate(NorVolume& volume, NorOpenFile& file, uint32_t size, const infra::Function<void(Result result)>& onDone);
        void StartNorFormat(NorVolume& volume, const infra::Function<void(Result result)>& onDone);
        void StartNorOverwrite(NorVolume& volume, NorOpenFile& file, infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone);
        void StartNorOpenDirectory(NorVolume& volume, NorOpenDirectoryCall&& call, const NorDirectoryAllocation& allocate, const infra::Function<void(Result result)>& onFinished);

        template<bool Enabled>
        struct NorBackgroundStorage
        {
            NorBackgroundStorage(NorVolume& volume, infra::Duration)
                : leveling(volume)
            {
                volume.AttachBackground(leveling);
            }

            NorForegroundLeveling leveling;
        };

        template<>
        struct NorBackgroundStorage<true>
        {
            NorBackgroundStorage(NorVolume& volume, infra::Duration idleDelay)
                : background(volume, idleDelay)
            {
                volume.AttachBackground(background);
            }

            NorBackground background;
        };

        template<bool Enabled, std::size_t Directories>
        struct NorDirectoryStorage
        {
            NorDirectoryStorage() = default;

            explicit NorDirectoryStorage(NorVolume&)
            {}

            infra::SharedPtr<NorDirectory> Allocate()
            {
                return nullptr;
            }
        };

        template<std::size_t Directories>
        struct NorDirectoryStorage<true, Directories>
        {
            explicit NorDirectoryStorage(NorVolume& volume)
                : pool(volume)
            {}

            infra::SharedPtr<NorDirectory> Allocate()
            {
                return pool.Allocate();
            }

            HandlePool<NorDirectory, Directories> pool;
        };
    }

    template<NorFeatures Features>
    constexpr NorOperations NorOperationsFor()
    {
        NorOperations operations;

        if constexpr (Features.makeDirectory)
            operations.makeDirectory = &detail::StartNorOperation<NorMakeDirectory, NorMakeDirectoryCall>;
        if constexpr (Features.remove)
            operations.remove = &detail::StartNorOperation<NorRemove, NorRemoveCall>;
        if constexpr (Features.rename)
            operations.rename = &detail::StartNorOperation<NorRename, NorRenameCall>;
        if constexpr (Features.truncate)
            operations.truncate = &detail::StartNorTruncate;
        if constexpr (Features.format)
            operations.format = &detail::StartNorFormat;
        if constexpr (Features.overwrite)
            operations.overwrite = &detail::StartNorOverwrite;
        if constexpr (Features.openDirectory)
            operations.openDirectory = &detail::StartNorOpenDirectory;

        return operations;
    }

    template<NorConfig Config, std::size_t Files, NorFeatures Features>
    class NorFileSystem::WithStorage
        : private detail::NorVolumeStorage<Config, Files + (Features.openDirectory ? Config.directories : 0) + Config.pathOperations + 3>
        , public NorFileSystem
    {
        static_assert(IsValidNorConfig(Config));
        static_assert(Files > 0 && (Config.directories > 0 || !Features.openDirectory));
        static_assert(NorFormatter::BufferBytes(Config) <= Config.cachedNodes * Config.nodeSize);

        using Storage = detail::NorVolumeStorage<Config, Files + (Features.openDirectory ? Config.directories : 0) + Config.pathOperations + 3>;

    public:
        explicit WithStorage(hal::Flash& flash, infra::Duration idleDelay = defaultIdleDelay)
            : NorFileSystem(NorVolumeParts{ flash, this->deferredStorage, infra::MakeRange(this->blockStateStorage), infra::MakeRange(this->eraseCountStorage), infra::MakeRange(this->nodeBlockStorage), infra::MakeRange(this->journalStorage), infra::MakeRange(this->nodeStorage), infra::MakeRange(this->nodeSlotStorage), operations }, Config)
            , background(Volume(), idleDelay)
        {
            for (std::size_t index = 0; index != Config.pathOperations; ++index)
                pathSlots.emplace_back(Volume().Queue());

            AttachPathSlots(infra::MakeRange(pathSlots));
        }

    private:
        static constexpr NorOperations operations = NorOperationsFor<Features>();

        infra::SharedPtr<NorDirectory> AllocateDirectory() override
        {
            return directories.Allocate();
        }

        infra::SharedPtr<NorFile> AllocateFile() override
        {
            return files.Allocate();
        }

    private:
        typename infra::BoundedVector<NorPathSlot>::template WithMaxSize<Config.pathOperations> pathSlots;
        detail::NorDirectoryStorage<Features.openDirectory, Config.directories> directories{ Volume() };
        HandlePool<NorFile::WithBuffer<NorFileBufferBytes(Config)>, Files> files{ Volume() };
        detail::NorBackgroundStorage<Features.background> background;
    };

    constexpr std::array<NorFileSystem::Machine::Transition, 9> NorFileSystem::Rows()
    {
        return {
            Machine::Row<NorUnmounted, NorFormatRequested, NorFormatting>(nullptr, [](NorFileSystem& volume, NorUnmounted&, const NorFormatRequested&)
                {
                    volume.BeginFormat();
                    return NorFormatting{};
                }),
            Machine::Row<NorFault, NorFormatRequested, NorFormatting>(nullptr, [](NorFileSystem& volume, NorFault&, const NorFormatRequested&)
                {
                    volume.BeginFormat();
                    return NorFormatting{};
                }),
            Machine::Row<NorFormatting, NorFormatDone, NorUnmounted>(nullptr, [](NorFileSystem& volume, NorFormatting&, const NorFormatDone& done)
                {
                    volume.lifecycleResult = done.result;
                    return NorUnmounted{};
                }),
            Machine::Row<NorUnmounted, NorMountRequested, NorMounting>(nullptr, [](NorFileSystem& volume, NorUnmounted&, const NorMountRequested&)
                {
                    volume.BeginMount();
                    return NorMounting{};
                }),
            Machine::Row<NorFault, NorMountRequested, NorMounting>(nullptr, [](NorFileSystem& volume, NorFault&, const NorMountRequested&)
                {
                    volume.BeginMount();
                    return NorMounting{};
                }),
            Machine::Row<NorMounting, NorMountDone, NorMounted>([](NorFileSystem&, const NorMounting&, const NorMountDone& done)
                {
                    return done.result == Result::success;
                }),
            Machine::Row<NorMounting, NorMountDone, NorFault>(nullptr, [](NorFileSystem&, NorMounting&, const NorMountDone& done)
                {
                    return NorFault{ done.result };
                }),
            Machine::Row<NorMounted, NorUnmountRequested, NorUnmounting>(nullptr, [](NorFileSystem& volume, NorMounted&, const NorUnmountRequested&)
                {
                    volume.BeginUnmount();
                    return NorUnmounting{};
                }),
            Machine::Row<NorUnmounting, NorUnmountDone, NorUnmounted>(nullptr, [](NorFileSystem& volume, NorUnmounting&, const NorUnmountDone& done)
                {
                    volume.lifecycleResult = done.result;
                    return NorUnmounted{};
                }),
        };
    }

    constexpr NorFileSystem::Machine::Rules NorFileSystem::Rules()
    {
        return Machine::Rules{}
            .Allow<NorUnmounted, NorFormatting, NorMounting>()
            .Allow<NorFormatting, NorUnmounted>()
            .Allow<NorMounting, NorMounted, NorFault>()
            .Allow<NorMounted, NorUnmounting>()
            .Allow<NorUnmounting, NorUnmounted>()
            .Allow<NorFault, NorFormatting, NorMounting>();
    }
}

#endif
